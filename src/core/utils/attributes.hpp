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
