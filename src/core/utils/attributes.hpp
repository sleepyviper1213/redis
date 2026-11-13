#pragma once

/// [[no_unique_address]], spelled the way the compiler honours it: MSVC and
/// clang-cl accept the standard spelling but ignore it, and only lay out an
/// empty member (a stateless allocator, say) in zero bytes under
/// [[msvc::no_unique_address]].
#ifdef __has_cpp_attribute
#if __has_cpp_attribute(msvc::no_unique_address)
#define REDIS_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#elif __has_cpp_attribute(no_unique_address)
#define REDIS_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif
#endif

#ifndef REDIS_NO_UNIQUE_ADDRESS
#define REDIS_NO_UNIQUE_ADDRESS
#endif

/// [[lifetimebound]]: the result refers to this parameter (or, after a member
/// function's parameter list, to the object), so the compiler warns when it
/// outlives an argument that is plainly a temporary or a local. It cannot see
/// lifetimes decided at run time, such as an operation state still running
/// after its owner is gone. clang (clang-cl too) and MSVC spell it in their
/// own namespace; GCC has no such attribute, and it is a no-op there.
#ifdef __has_cpp_attribute
#if __has_cpp_attribute(clang::lifetimebound)
#define REDIS_LIFETIMEBOUND [[clang::lifetimebound]]
#elif __has_cpp_attribute(msvc::lifetimebound)
#define REDIS_LIFETIMEBOUND [[msvc::lifetimebound]]
#endif
#endif

#ifndef REDIS_LIFETIMEBOUND
#define REDIS_LIFETIMEBOUND
#endif

/// [[gnu::const]]: return depends only on arguments; no memory reads.
/// [[gnu::pure]]:  return depends on arguments and readable memory, no writes.
/// [[gnu::hot]]:   function is on a hot path; optimise more aggressively.
/// All are no-ops on compilers that do not understand GNU attributes.
#if defined(__GNUC__) || defined(__clang__)
#define REDIS_CONST_FN [[gnu::const]]
#define REDIS_PURE_FN  [[gnu::pure]]
#define REDIS_HOT_FN   [[gnu::hot]]
#else
#define REDIS_CONST_FN
#define REDIS_PURE_FN
#define REDIS_HOT_FN
#endif
