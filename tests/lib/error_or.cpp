#include "lib/error_or.hpp"

#include "lib/forward.hpp"

#include <catch2/catch_template_test_macros.hpp>

using namespace AK;

SCENARIO("Using ErrorOr with values, references, and errors", "[ErrorOr]") {
	GIVEN("An ErrorOr<int> holding a value") {
		ErrorOr<int> value_or{42};

		THEN("It should not be an error") { REQUIRE(!value_or.is_error()); }

		AND_THEN("The value can be accessed") {
			REQUIRE(value_or.value() == 42);
		}

		AND_THEN("It can be moved") {
			auto moved = std::move(value_or);
			REQUIRE(!moved.is_error());
			REQUIRE(moved.value() == 42);
		}
	}

	GIVEN("An ErrorOr<int> holding an error") {
		Error err = Error::from_errno(1);
		ErrorOr<int> error_or{std::move(err)};

		THEN("It should be an error") { REQUIRE(error_or.is_error()); }

		AND_THEN("The error can be accessed") {
			REQUIRE(error_or.error() == err);
		}

		AND_THEN("It can be moved") {
			auto moved = std::move(error_or);
			REQUIRE(moved.is_error());
			REQUIRE(moved.error() == err);
		}
	}

	GIVEN("An ErrorOr<int&> holding a reference") {
		ErrorOr<int> x                             = 99;
		ErrorOr<const int &, const Error &> ref_or = x.as_ref();

		THEN("It should not be an error") { REQUIRE(!ref_or.is_error()); }

		AND_THEN("The reference points to the original object") {
			REQUIRE(&ref_or.value() == &x.value());
			ref_or.value() = 100;
			REQUIRE(x == 100);
		}
	}

	GIVEN("An ErrorOr<int&> holding an error") {
		Error err = Error::from_errno(2);
		ErrorOr<int &> error_ref_or{err};

		THEN("It should be an error") {
			REQUIRE(error_ref_or.is_error());
			REQUIRE(error_ref_or.error() == err);
		}
	}

	GIVEN("An ErrorOr<void>") {
		WHEN("Constructed as success") {
			ErrorOr<void> success;
			THEN("It should not be an error") { REQUIRE(!success.is_error()); }
		}

		WHEN("Constructed with an error") {
			Error err = Error::from_errno(3);
			ErrorOr<void> failure{err};
			THEN("It should be an error") {
				REQUIRE(failure.is_error());
				REQUIRE(failure.error() == err);
			}
		}
	}

	GIVEN("Converting ErrorOr<U> to ErrorOr<T>") {
		ErrorOr<int> original{123};
		ErrorOr<long> converted{std::move(original)};
		THEN("Value should be converted correctly") {
			REQUIRE(!converted.is_error());
			REQUIRE(converted.value() == 123);
		}

		Error err = Error::from_errno(4);
		ErrorOr<int> err_or{err};
		ErrorOr<long> converted_err{std::move(err_or)};
		THEN("Error should propagate correctly") {
			REQUIRE(converted_err.is_error());
			REQUIRE(converted_err.error() == err);
		}
	}
}

SCENARIO("ErrorOr works with non-trivial types", "[ErrorOr][non-trivial]") {
	GIVEN("An ErrorOr<std::string> holding a value") {
		ErrorOr<std::string> str_or{std::string("Hello")};

		THEN("It should not be an error") { REQUIRE(!str_or.is_error()); }

		AND_THEN("The value can be accessed") {
			REQUIRE(str_or.value() == "Hello");
		}

		AND_THEN("It can be moved") {
			auto moved = std::move(str_or);
			REQUIRE(!moved.is_error());
			REQUIRE(moved.value() == "Hello");
		}
	}

	GIVEN("An ErrorOr<std::string> holding an error") {
		Error err = Error::from_errno(5);
		ErrorOr<std::string> error_or{err};

		THEN("It should be an error") {
			REQUIRE(error_or.is_error());
			REQUIRE(error_or.error() == err);
		}

		AND_THEN("It can be moved") {
			auto moved = std::move(error_or);
			REQUIRE(moved.is_error());
			REQUIRE(moved.error() == err);
		}
	}

	GIVEN("An ErrorOr<std::vector<int>> holding a value") {
		std::vector<int> vec{1, 2, 3};
		ErrorOr<std::vector<int>> vec_or{vec};

		THEN("It should not be an error") { REQUIRE(!vec_or.is_error()); }

		AND_THEN("The value can be accessed and modified") {
			REQUIRE(vec_or.value() == vec);
			vec_or.value().push_back(4);
			REQUIRE(vec_or.value().size() == 4);
		}

		AND_THEN("It can be moved") {
			auto moved = std::move(vec_or);
			REQUIRE(!moved.is_error());
			REQUIRE(moved.value().size() == 4);
		}
	}

	GIVEN("An ErrorOr<std::string&> holding a reference") {
		std::string str = "World";
		ErrorOr<std::string &> str_ref_or{str};

		THEN("It should not be an error") { REQUIRE(!str_ref_or.is_error()); }

		AND_THEN("Modifying the reference changes the original object") {
			str_ref_or.value() = "Modified";
			REQUIRE(str == "Modified");
		}
	}

	GIVEN("An ErrorOr<std::vector<int>&> holding a reference") {
		std::vector<int> vec{10, 20};
		ErrorOr<std::vector<int> &> vec_ref_or{vec};

		THEN("It should not be an error") { REQUIRE(!vec_ref_or.is_error()); }

		AND_THEN("Modifying the reference changes the original vector") {
			vec_ref_or.value().push_back(30);
			REQUIRE(vec.size() == 3);
			REQUIRE(vec[2] == 30);
		}
	}

	GIVEN("An ErrorOr<std::string&> holding an error") {
		Error err = Error::from_errno(6);
		ErrorOr<std::string &> error_ref_or{err};

		THEN("It should be an error") {
			REQUIRE(error_ref_or.is_error());
			REQUIRE(error_ref_or.error() == err);
		}
	}

	GIVEN("An ErrorOr<std::vector<int>&> holding an error") {
		Error err = Error::from_errno(7);
		ErrorOr<std::vector<int> &> error_ref_or{err};

		THEN("It should be an error") {
			REQUIRE(error_ref_or.is_error());
			REQUIRE(error_ref_or.error() == err);
		}
	}
}

// Compile-time checks for non-trivial types
STATIC_REQUIRE(std::is_constructible_v<ErrorOr<std::string &>, std::string &>);
STATIC_REQUIRE(!std::is_constructible_v<ErrorOr<std::string &>, std::string>);
STATIC_REQUIRE(
	std::is_constructible_v<ErrorOr<std::vector<int> &>, std::vector<int> &>);
STATIC_REQUIRE(
	!std::is_constructible_v<ErrorOr<std::vector<int> &>, std::vector<int>>);
STATIC_REQUIRE(std::is_constructible_v<ErrorOr<std::string>, std::string>);
STATIC_REQUIRE(
	std::is_constructible_v<ErrorOr<std::vector<int>>, std::vector<int>>);

// Compile-time checks
STATIC_REQUIRE(std::is_constructible_v<ErrorOr<int &>, int &>);
STATIC_REQUIRE(!std::is_constructible_v<ErrorOr<int &>, int>);
STATIC_REQUIRE(std::is_constructible_v<ErrorOr<int>, int>);
STATIC_REQUIRE(std::is_constructible_v<ErrorOr<int>, int &>);
STATIC_REQUIRE(std::is_constructible_v<ErrorOr<const int>, const int &>);
STATIC_REQUIRE(std::is_constructible_v<ErrorOr<const int>, int>);
