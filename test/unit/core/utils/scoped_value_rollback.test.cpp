#include "core/utils/scoped_value_rollback.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

using redis::ScopedValueRollback;

TEST_CASE("A scoped rollback restores the value when the scope ends",
		  "[utils][scoped_value_rollback]") {
	int value = 1;
	{
		ScopedValueRollback rollback(value);
		value = 2;
		CHECK(value == 2);
	}
	CHECK(value == 1);
}

TEST_CASE("A scoped rollback restores the value when an exception leaves the scope",
		  "[utils][scoped_value_rollback]") {
	std::string text = "before";
	try {
		ScopedValueRollback rollback(text);
		text = "during";
		throw std::runtime_error("leave the scope");
	} catch (const std::runtime_error &) {}
	CHECK(text == "before");
}

TEST_CASE("The rollback value can be replaced to keep a change",
		  "[utils][scoped_value_rollback]") {
	int value = 1;
	{
		ScopedValueRollback rollback(value);
		value = 2;
		rollback.set_rollback_value(value);
		value = 3;
	}
	CHECK(value == 2);
}

TEST_CASE("Scoped rollbacks nest", "[utils][scoped_value_rollback]") {
	int value = 1;
	{
		ScopedValueRollback outer(value);
		value = 2;
		{
			ScopedValueRollback inner(value);
			value = 3;
		}
		CHECK(value == 2);
	}
	CHECK(value == 1);
}
