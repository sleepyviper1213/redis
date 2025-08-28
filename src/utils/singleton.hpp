#pragma once

#include <mutex>
#include <type_traits>

namespace redis::global {
template <typename Derived>
class Singleton {
public:
	template <typename... Args>
	static Derived &
	get_instance() noexcept(std::is_nothrow_constructible_v<Derived>) {
		std::call_once(init_flag_, &Derived::init, std::forward<Args>...);
		return *instance_;
	}

	static void shutdown() {
		std::lock_guard<std::mutex> lock(mutex_);
		instance_.reset();
	}

	Singleton(Singleton &&)                 = delete;
	Singleton &operator=(Singleton &&)      = delete;
	Singleton(const Singleton &)            = delete;
	Singleton &operator=(const Singleton &) = delete;

protected:
	Singleton() = default;

	template <typename... Args>
	static void init(Args &&...args) {
		instance_ = std::make_unique<Derived>(std::forward<Args>(args)...);
	}

private:
	static constinit std::unique_ptr<Derived> instance_;
	static constinit std::once_flag init_flag_;
	static std::mutex mutex_;
};

// Static member initialization
template <typename T>
constinit std::unique_ptr<T> Singleton<T>::instance_;

template <typename T>
constinit std::once_flag Singleton<T>::init_flag_;

template <typename T>
std::mutex Singleton<T>::mutex_;
} // namespace redis::global
