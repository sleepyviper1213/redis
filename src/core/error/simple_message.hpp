#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace redis {

class [[nodiscard]] RedisError {
public:
	// --- Enum for common structured errors ---
	enum class [[nodiscard]] Kind {
		UNKNOWN_COMMAND,
		WRONG_TYPE,
		OOM,
		MISCONF,
		READ_ONLY,
		LOADING,
		NO_AUTH,
		WRONG_PASS,
		CLUSTER_DOWN,
		MASTER_DOWN,
		AOF_WRITE_ERROR,
		MOVED,
		ASK,
		TIMEOUT,
	};

	// --- Variant payload ---
	using ErrorData =
		std::variant<Kind,        // Plain enum error
					 std::string, // Free-form static/dynamic message
					 std::unordered_map<std::string, std::string> // Metadata
					 >;

	explicit RedisError(Kind kind);

	explicit RedisError(std::string msg);

	RedisError(Kind kind, std::unordered_map<std::string, std::string> meta);

	// Retrieve kind if present
	[[nodiscard]] const Kind *kind() const noexcept;

	// Retrieve message if present
	[[nodiscard]] std::string_view message() const;

	// Retrieve metadata if present
	[[nodiscard]] const std::unordered_map<std::string, std::string> *
	metadata() const;

	// Convert to RESP-style error string

private:
	ErrorData data_;
	std::optional<Kind> kind_for_meta_; // only needed if variant holds metadata
};

[[nodiscard]] std::string format_as(const RedisError &error);
} // namespace redis
