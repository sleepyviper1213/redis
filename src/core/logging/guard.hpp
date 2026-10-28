#pragma once

#include "core/logging/settings.hpp"

// The scoped form of init/shutdown.
namespace redis::logging {

/**
 * @brief Scoped logging: @c init on construction, @c shutdown on destruction.
 *
 * Ending the logger with the scope, not merely flushing it, is what joins the
 * async worker thread on an ordinary running thread instead of during static
 * destruction, where it can deadlock. @see logging::shutdown
 *
 * @warning The default logger is null once this destructs, so nothing may log
 *          afterwards. Declare the guard in @c main and keep log calls inside
 *          its scope:
 *
 * @code
 * int main(int argc, char **argv) {
 *     const redis::logging::guard log{{.level = "info"}};
 *     ...              // flushed at the end of main, on every return path
 * }
 * @endcode
 *
 * @warning One per process, declared before anything logs.
 */
class guard {
public:
	explicit guard(const settings &config);
	~guard();

	// Owns process-wide state, so neither copyable nor movable.
	guard(const guard &)            = delete;
	guard &operator=(const guard &) = delete;
	guard(guard &&)                 = delete;
	guard &operator=(guard &&)      = delete;
};

} // namespace redis::logging
