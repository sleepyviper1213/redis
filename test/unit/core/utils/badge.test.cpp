#include "core/utils/badge.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

using redis::Badge;

namespace {
class Door;

/// Makes the Badge<Owner> that Door::open asks for.
class Owner {
public:
	static bool open(Door &door);
};

class Door {
public:
	bool open(Badge<Owner> /*unused*/) noexcept { return true; }
};

bool Owner::open(Door &door) { return door.open({}); }

/// Only Owner may make one; nobody may copy or move one.
static_assert(!std::is_default_constructible_v<Badge<Owner>>);
static_assert(!std::is_copy_constructible_v<Badge<Owner>>);
static_assert(!std::is_move_constructible_v<Badge<Owner>>);
} // namespace

SCENARIO("The class a Badge names can call through it", "[badge]") {
	Door door;
	CHECK(Owner::open(door));
}
