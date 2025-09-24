#pragma once

#if defined(_MSC_VER) // MSVC
#define LIFETIMEBOUND [[msvc::lifetimebound]]
#include <CppCoreCheck/Warnings.h>
#pragma warning(                                                               \
	default : CPPCORECHECK_LIFETIME_WARNINGS) // Enable lifetimebound warnings
#elif defined(__clang__)
#define LIFETIMEBOUND [[clang::lifetimebound]]
#elif defined(__GNUC__)
#define LIFETIMEBOUND
#endif
