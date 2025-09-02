#pragma once

#include <type_traits>

namespace redis::global {
template <typename Derived>
class MayersSingleton {
public:
	MayersSingleton(const MayersSingleton &)            = delete;
	MayersSingleton(MayersSingleton &&)                 = delete;
	MayersSingleton &operator=(const MayersSingleton &) = delete;
	MayersSingleton &operator=(MayersSingleton &&)      = delete;

	template <typename... Args>
	static Derived &get_instance(Args &&...args) noexcept(
		std::is_nothrow_constructible_v<Derived>) {
		static Derived instance(std::forward<Args>(args)...); // construct once
		return instance;
	}

protected:
	MayersSingleton() = default;
};
} // namespace redis::global
