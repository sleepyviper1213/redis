#include "core/logging/lifecycle.hpp"

#include "core/logging/channels.hpp"
#include "core/logging/settings.hpp"

#include <fmt/format.h>
#include <spdlog/async.h>
#include <spdlog/async_logger.h>
#include <spdlog/formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace redis::logging {
namespace {

/**
 * @brief One JSON object per line: @c {"ts_ms":...,"level":"...",
 *        "logger":"...","msg":"..."}.
 *
 * @c msg.payload is the one field that can legitimately contain a quote, a
 * backslash or a newline - log lines quote client keys - so it is the one
 * field @c escape_json runs over.
 *
 * @c ts_ms rather than a formatted timestamp: milliseconds since the epoch is
 * unambiguous across every consumer's timezone and locale.
 */
class json_line_formatter final : public spdlog::formatter {
public:
	void format(const spdlog::details::log_msg &msg,
				spdlog::memory_buf_t &dest) final {
		const auto epoch_ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(
				msg.time.time_since_epoch())
				.count();
		fmt::format_to(fmt::appender(dest),
					   R"({{"ts_ms":{},"level":"{}","logger":"{}","msg":")",
					   epoch_ms,
					   spdlog::level::to_string_view(msg.level),
					   msg.logger_name);
		escape_json(msg.payload, dest);
		fmt::format_to(fmt::appender(dest), "\"}}\n");
	}

	[[nodiscard]] std::unique_ptr<spdlog::formatter> clone() const final {
		return std::make_unique<json_line_formatter>();
	}

private:
	/// @brief Append @p text to @p dest with the characters JSON forbids
	///        unescaped turned into their escape sequence.
	static void escape_json(spdlog::string_view_t text,
							spdlog::memory_buf_t &dest) {
		for (const char c : text) {
			switch (c) {
			case '"': fmt::format_to(fmt::appender(dest), "\\\""); break;
			case '\\': fmt::format_to(fmt::appender(dest), "\\\\"); break;
			case '\n': fmt::format_to(fmt::appender(dest), "\\n"); break;
			case '\r': fmt::format_to(fmt::appender(dest), "\\r"); break;
			case '\t': fmt::format_to(fmt::appender(dest), "\\t"); break;
			default:
				// The rest of the C0 control range is not legal inside a JSON
				// string unescaped.
				if (static_cast<unsigned char>(c) < 0x20)
					fmt::format_to(fmt::appender(dest),
								   "\\u{:04x}",
								   static_cast<unsigned char>(c));
				else dest.push_back(c);
			}
		}
	}
};

/// @brief The outcome of reading a level name, keeping "what it is" separate
///        from "was it what you asked for".
struct level_choice {
	spdlog::level::level_enum level = spdlog::level::info;
	bool recognised                 = true;
};

/**
 * @brief Turn a level name into a level.
 *
 * Exists because @c spdlog::level::from_str answers @c off for anything it
 * does not recognise, which makes a typo indistinguishable from deliberately
 * silencing the log - one wrong character in REDIS_LOGLEVEL and the server
 * runs with logging disabled and says nothing about it.
 */
[[nodiscard]] level_choice read_level(const std::string &name) {
	const auto parsed = spdlog::level::from_str(name);
	if (parsed != spdlog::level::off || name == "off") return {parsed, true};
	return {spdlog::level::info, false};
}

/**
 * @brief Build the sink set @p config asks for.
 *
 * @param[out] degraded Empty when every requested sink was created; otherwise
 *        describes what was lost and why.
 *
 * Returning the problem instead of logging it is the point: this runs before
 * the logger it is building is installed, so a log call here would go to
 * spdlog's built-in logger, which writes to stdout. The caller emits it once
 * the real logger is in place.
 */
[[nodiscard]] std::vector<spdlog::sink_ptr> make_sinks(const settings &config,
													   std::string &degraded) {
	std::vector<spdlog::sink_ptr> sinks;
	sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());

	if (config.log_file.empty()) return sinks;

	// A log file that cannot be opened throws. That must not stop the server:
	// losing the file sink costs a log, failing to start costs the service.
	try {
		sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(
			config.log_file));
	} catch (const std::exception &error) {
		degraded = fmt::format("log file '{}' unavailable ({}); stderr only",
							   config.log_file,
							   error.what());
	}
	return sinks;
}

/**
 * @brief Build the logger object itself - sync or async - over @p sinks.
 *
 * @c overrun_oldest, never @c block: the queue fills exactly when the server
 * is busiest, and blocking there would stall the event loop - every client -
 * on a diagnostic. @see settings::async_queue
 *
 * @note @c async_logger overrides @c clone, so the channels derived from this
 *       logger are asynchronous too.
 */
[[nodiscard]] std::shared_ptr<spdlog::logger>
make_logger_object(const settings &config,
				   std::vector<spdlog::sink_ptr> sinks) {
	if (!config.async)
		return std::make_shared<spdlog::logger>(config.logger_name,
												sinks.begin(),
												sinks.end());

	// Before the logger, which holds the pool by shared_ptr. A second `init`
	// installs a fresh pool and leaves the previous one alive for as long as
	// any logger still refers to it.
	spdlog::init_thread_pool(config.async_queue, 1);
	return std::make_shared<spdlog::async_logger>(
		config.logger_name,
		sinks.begin(),
		sinks.end(),
		spdlog::thread_pool(),
		spdlog::async_overflow_policy::overrun_oldest);
}

/// @brief Assemble a logger over @p sinks with @p config's presentation and
///        buffering policy. Does not install it.
[[nodiscard]] std::shared_ptr<spdlog::logger>
make_logger(const settings &config, std::vector<spdlog::sink_ptr> sinks,
			spdlog::level::level_enum level) {
	auto logger = make_logger_object(config, std::move(sinks));
	if (config.structured) {
		logger->set_formatter(std::make_unique<json_line_formatter>());
	} else {
		// %^…%$ colours the console sink; the file sink ignores the markers.
		// %n is the logger's name, so every line says which channel wrote it.
		logger->set_pattern("%^[%T.%e] [%n] [%l]%$ %v");
	}
	logger->set_level(level);
	logger->flush_on(spdlog::level::warn);
	if (config.backtrace > 0) logger->enable_backtrace(config.backtrace);
	return logger;
}

} // namespace

void init(const settings &config) {
	std::string degraded;
	auto sinks                = make_sinks(config, degraded);
	const auto [level, known] = read_level(config.level);
	spdlog::set_default_logger(make_logger(config, std::move(sinks), level));
	// Immediately after, so every channel carries the sinks and level this
	// call just chose. @see channels.hpp
	install_channels();

	// Emitted only now, through the logger just installed, so they land on the
	// sinks the caller asked for.
	if (!degraded.empty()) spdlog::warn("{}", degraded);
	if (!known)
		spdlog::warn("unknown log level '{}'; falling back to info",
					 config.level);

	const char *const delivery = config.async ? "async" : "sync";
	if (config.log_file.empty())
		spdlog::debug("logging initialised at level '{}' ({}, stderr)",
					  config.level,
					  delivery);
	else
		spdlog::debug("logging initialised at level '{}' ({}, stderr + {})",
					  config.level,
					  delivery,
					  config.log_file);
}

void dump_backtrace() noexcept {
	try {
		if (const auto &logger = spdlog::default_logger())
			logger->dump_backtrace();
	} catch (...) {} // NOLINT(bugprone-empty-catch)
}

void flush() noexcept {
	try {
		if (const auto &logger = spdlog::default_logger()) logger->flush();
	} catch (...) {} // NOLINT(bugprone-empty-catch)
}

void shutdown() noexcept {
	flush();
	try {
		spdlog::shutdown();
	} catch (...) {} // NOLINT(bugprone-empty-catch)
}

} // namespace redis::logging
