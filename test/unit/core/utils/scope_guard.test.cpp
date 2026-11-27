#include "core/utils/scope_guard.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using redis::ScopeGuard;

namespace {
int calls = 0;
void count_call() noexcept { ++calls; }
} // namespace

TEST_CASE("A scope guard runs its function when the scope ends",
		  "[utils][scope_guard]") {
	calls = 0;
	{
		const ScopeGuard guard{&count_call};
		CHECK(calls == 0);
	}
	CHECK(calls == 1);
}

TEST_CASE("A scope guard runs when an exception leaves the scope",
		  "[utils][scope_guard]") {
	calls = 0;
	try {
		const ScopeGuard guard{&count_call};
		throw std::runtime_error("leave the scope");
	} catch (const std::runtime_error &) {}
	CHECK(calls == 1);
}

TEST_CASE("Scope guards run innermost first", "[utils][scope_guard]") {
	int order = 0;
	int outer_at = 0;
	int inner_at = 0;
	{
		const auto note_outer = [&]() noexcept { outer_at = ++order; };
		const auto note_inner = [&]() noexcept { inner_at = ++order; };
		const ScopeGuard outer{note_outer};
		const ScopeGuard inner{note_inner};
	}
	CHECK(inner_at == 1);
	CHECK(outer_at == 2);
}
