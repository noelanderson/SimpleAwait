#pragma once

// SimpleAwait — compile-time coroutine support checks.
//
// SimpleAwait requires native C++20 standard coroutines. This header verifies
// that the active toolchain provides them and then includes <coroutine>.
//
// Rationale for the checks used here:
//   * We intentionally do NOT gate on __cplusplus. Some conforming compilers
//     (notably MSVC without /Zc:__cplusplus) report an inaccurate __cplusplus
//     value even when C++20 coroutines are fully available. The specification
//     explicitly forbids determining compatibility solely through __cplusplus.
//   * __cpp_impl_coroutine is the language feature-test macro defined by any
//     compiler that implements the coroutines core language feature.
//   * The <coroutine> header must be present. Where the standard library also
//     advertises __cpp_lib_coroutine we surface it, but its absence alone is
//     not treated as fatal because the language feature plus a usable header is
//     what SimpleAwait actually depends on.

#if !defined(__cpp_impl_coroutine)
#  error "SimpleAwait requires C++20 or later with standard coroutine support"
#endif

#if defined(__has_include)
#  if !__has_include(<coroutine>)
#    error "SimpleAwait requires C++20 or later with standard coroutine support (<coroutine> not found)"
#  endif
#endif

#include <coroutine>

// A minimal sanity guard: the standard coroutine primitives must be usable.
// This does not execute anything; it only forces the names to resolve so a
// broken/experimental-only coroutine environment fails at include time rather
// than deep inside an unrelated template instantiation.
namespace simpleawait {
namespace detail {

using coroutine_support_probe = ::std::suspend_always;

} // namespace detail
} // namespace simpleawait
