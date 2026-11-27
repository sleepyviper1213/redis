#pragma once

#include <type_traits>
#include <utility>

namespace redis {

/**
 * \brief Puts a variable back to the value it had when the scope was entered.
 *
 * The variable keeps whatever the scope assigns to it until the scope ends,
 * on any path (return, early exit, exception). set_rollback_value() changes
 * what is restored, for a scope that commits its change on success.
 *
 * Not copyable or movable: a copy would restore twice. Moving the saved value
 * back must not throw, as it runs in a destructor.
 */
template <typename T>
	requires std::is_nothrow_move_assignable_v<T>
class ScopedValueRollback {
public:
	explicit ScopedValueRollback(T &variable)
		: variable_(variable), saved_(variable) {}

	ScopedValueRollback(const ScopedValueRollback &)            = delete;
	ScopedValueRollback &operator=(const ScopedValueRollback &) = delete;

	~ScopedValueRollback() { variable_ = std::move(saved_); }

	void set_rollback_value(const T &value) { saved_ = value; }

private:
	T &variable_;
	T saved_;
};

} // namespace redis
