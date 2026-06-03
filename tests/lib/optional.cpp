#include "lib/optional.hpp"

#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <string>
#include <type_traits>
#include <vector>

using namespace AK;

SCENARIO("AK::Optional<T> basic usage", "[optional]") {
	GIVEN("A default-constructed Optional<int>") {
		Optional<int> opt;

		THEN("It has no value") { REQUIRE(!opt.has_value()); }

		WHEN("Assigning a value") {
			opt = 42;

			THEN("It stores the value") {
				REQUIRE(opt.has_value());
				REQUIRE(opt.value() == 42);
			}

			AND_WHEN("Value is cleared using OptionalNone") {
				opt = detail::EmptyOptional{};
				THEN("It has no value") { REQUIRE(!opt.has_value()); }
			}
		}

		WHEN("Emplacing a value") {
			opt.emplace(123);
			THEN("It has the new value") {
				REQUIRE(opt.has_value());
				REQUIRE(opt.value() == 123);
			}

			AND_WHEN("Lazy emplace is used") {
				opt.lazy_emplace([]() { return 456; });
				THEN("It has the value returned by the callable") {
					REQUIRE(opt.has_value());
					REQUIRE(opt.value() == 456);
				}
			}
		}
	}

	GIVEN("An Optional<int> initialized with a value") {
		Optional<int> opt(10);

		THEN("It has the correct value") {
			REQUIRE(opt.has_value());
			REQUIRE(opt.value() == 10);
		}

		WHEN("Copying the Optional") {
			Optional<int> copy = opt;

			THEN("The copy has the same value") {
				REQUIRE(copy.has_value());
				REQUIRE(copy.value() == 10);
			}
		}

		WHEN("Moving the Optional") {
			Optional<int> moved = std::move(opt);

			THEN("The moved Optional has the value") {
				REQUIRE(moved.has_value());
				REQUIRE(moved.value() == 10);
			}
		}

		WHEN("Assigning a new value") {
			opt = 99;

			THEN("The value is updated") {
				REQUIRE(opt.has_value());
				REQUIRE(opt.value() == 99);
			}
		}

		WHEN("Using value_or") {
			Optional<int> empty;
			THEN("It returns fallback if empty") {
				REQUIRE(empty.value_or(7) == 7);
			}

			THEN("It returns stored value if present") {
				REQUIRE(opt.value_or(7) == 10);
			}
		}

		WHEN("Releasing the value") {
			int released = opt.release_value();
			THEN("It returns the stored value") { REQUIRE(released == 10); }
			THEN("Optional becomes empty") { REQUIRE(!opt.has_value()); }
		}

		WHEN("Comparing Optionals") {
			Optional<int> a(10), b(10), c(20), d;

			THEN("Equality works") {
				REQUIRE(a == b);
				REQUIRE(!(a == c));
				REQUIRE(!(a == d));
				REQUIRE(a == 10);
				REQUIRE(!(a == 20));
			}
		}
	}

	GIVEN("An Optional<T&> reference") {
		int x = 5;
		Optional<int &> ref_opt(x);

		THEN("It holds a reference") {
			REQUIRE(ref_opt.has_value());
			REQUIRE(ref_opt.value() == 5);
		}

		WHEN("The referenced value changes") {
			x = 42;
			THEN("The Optional sees the change") {
				REQUIRE(ref_opt.value() == 42);
			}
		}

		WHEN("Assigning a new reference") {
			int y   = 7;
			ref_opt = y;
			THEN("It now refers to the new value") {
				REQUIRE(ref_opt.value() == 7);
			}
		}
	}

	GIVEN("Mapping an Optional") {
		Optional<int> opt(3);

		WHEN("Mapping to double the value") {
			auto mapped = opt.map([](int v) { return v * 2; });

			THEN("The mapped Optional has the transformed value") {
				REQUIRE(mapped.has_value());
				REQUIRE(mapped.value() == 6);
			}
		}

		WHEN("Mapping an empty Optional") {
			Optional<int> empty;
			auto mapped = empty.map([](int v) { return v * 2; });

			THEN("The mapped Optional is empty") {
				REQUIRE(!mapped.has_value());
			}
		}
	}
}

SCENARIO("AK::Optional<T> with non-trivial types", "[optional][non-trivial]") {
	GIVEN("An Optional<std::string>") {
		Optional<std::string> opt;

		THEN("It is empty initially") { REQUIRE(!opt.has_value()); }

		WHEN("Emplacing a string") {
			opt.emplace("hello");
			THEN("It stores the string") {
				REQUIRE(opt.has_value());
				REQUIRE(opt.value() == "hello");
			}

			AND_WHEN("Assigning a new string") {
				opt = std::string("world");
				THEN("Value is updated") {
					REQUIRE(opt.has_value());
					REQUIRE(opt.value() == "world");
				}
			}

			AND_WHEN("Copying the Optional") {
				Optional<std::string> copy = opt;
				THEN("Copy has the same value") {
					REQUIRE(copy.has_value());
					REQUIRE(copy.value() == "hello");
				}
			}

			AND_WHEN("Moving the Optional") {
				Optional<std::string> moved = std::move(opt);
				THEN(
					"Moved Optional has the value and original becomes empty") {
					REQUIRE(moved.has_value());
					REQUIRE(moved.value() == "hello");
					REQUIRE(!opt.has_value());

					AND_WHEN("Access no-value optional") {
						REQUIRE_THROWS(opt.value());
					}
				}
			}
		}
	}

	GIVEN("An Optional<std::vector<int>>") {
		Optional<std::vector<int>> opt_vec;

		THEN("It is empty initially") { REQUIRE(!opt_vec.has_value()); }

		WHEN("Emplacing a vector") {
			opt_vec.emplace({1, 2, 3});
			THEN("It stores the vector") {
				REQUIRE(opt_vec.has_value());
				REQUIRE(opt_vec.value() == std::vector<int>{1, 2, 3});
			}

			AND_WHEN("Assigning a new vector") {
				opt_vec = std::vector<int>{4, 5, 6};
				THEN("Value is updated") {
					REQUIRE(opt_vec.has_value());
					REQUIRE(opt_vec.value() == std::vector<int>{4, 5, 6});
				}
			}

			AND_WHEN("Copying the Optional") {
				Optional<std::vector<int>> copy = opt_vec;
				THEN("Copy has the same value") {
					REQUIRE(copy.has_value());
					REQUIRE(copy.value() == std::vector<int>{1, 2, 3});
				}
			}

			AND_WHEN("Moving the Optional") {
				Optional<std::vector<int>> moved = std::move(opt_vec);
				THEN("Moved Optional has the value and original becomes "
					 "empty") {
					REQUIRE(moved.has_value());
					REQUIRE(moved.value() == std::vector<int>{1, 2, 3});
					REQUIRE(!opt_vec.has_value());
				}
			}
		}

		WHEN("Clearing the vector Optional") {
			opt_vec.emplace({7, 8, 9});
			opt_vec = detail::EmptyOptional{};
			THEN("Optional becomes empty") { REQUIRE(!opt_vec.has_value()); }
		}
	}
}

SCENARIO("AK::Optional<T&> with non-trivial types",
		 "[optional][reference][non-trivial]") {
	GIVEN("An std::string variable") {
		std::string str = "original";
		Optional<std::string &> opt_ref(str);

		THEN("Optional holds a reference to the string") {
			REQUIRE(opt_ref.has_value());
			REQUIRE(&opt_ref.value() == &str);
			REQUIRE(opt_ref.value() == "original");
		}

		WHEN("Modifying the original string") {
			str = "changed";

			THEN("Optional sees the updated value") {
				REQUIRE(opt_ref.value() == "changed");
			}
		}

		WHEN("Modifying through the Optional reference") {
			opt_ref.value() = "modified";

			THEN("Original string is updated") { REQUIRE(str == "modified"); }
		}

		WHEN("Assigning a new reference") {
			std::string other = "new";
			opt_ref           = other;

			THEN("Optional now refers to the new string") {
				REQUIRE(&opt_ref.value() == &other);
				REQUIRE(opt_ref.value() == "new");
			}
		}

		WHEN("Clearing the Optional") {
			opt_ref = detail::EmptyOptional{};

			THEN("It no longer has a value") { REQUIRE(!opt_ref.has_value()); }
		}
	}

	GIVEN("An std::vector<int> variable") {
		std::vector<int> vec = {1, 2, 3};
		Optional<std::vector<int> &> opt_vec_ref(vec);

		THEN("Optional holds a reference to the vector") {
			REQUIRE(opt_vec_ref.has_value());
			REQUIRE(&opt_vec_ref.value() == &vec);
			REQUIRE(opt_vec_ref.value() == std::vector<int>{1, 2, 3});
		}

		WHEN("Modifying the original vector") {
			vec.push_back(4);

			THEN("Optional sees the updated vector") {
				REQUIRE(opt_vec_ref.value() == std::vector<int>{1, 2, 3, 4});
			}
		}

		WHEN("Modifying through the Optional reference") {
			opt_vec_ref.value().push_back(5);

			THEN("Original vector is updated") {
				REQUIRE(vec == std::vector<int>{1, 2, 3, 5});
			}
		}

		WHEN("Assigning a new reference") {
			std::vector<int> other = {7, 8};
			opt_vec_ref            = other;

			THEN("Optional now refers to the new vector") {
				REQUIRE(&opt_vec_ref.value() == &other);
				REQUIRE(opt_vec_ref.value() == std::vector<int>{7, 8});
			}
		}

		WHEN("Clearing the Optional") {
			opt_vec_ref = detail::EmptyOptional{};

			THEN("It no longer has a value") {
				REQUIRE(!opt_vec_ref.has_value());
			}
		}
	}
}

SCENARIO(
	"Optional<T>::operator* preserves const correctness and value category",
	"[optional][operator*]") {
	GIVEN("a non-const lvalue Optional<int>") {
		AK::Optional<int> o{42};

		WHEN("dereferenced") {
			auto &&val = *o;

			THEN("type is int&") {
				STATIC_REQUIRE(std::same_as<decltype(val), int &>);
			}

			THEN("assignable") {
				STATIC_REQUIRE(std::assignable_from<decltype(val), int>);
			}
		}
	}

	GIVEN("a const lvalue Optional<int>") {
		const AK::Optional<int> co{123};

		WHEN("dereferenced") {
			auto &val = *co;

			THEN("type is const int&") {
				STATIC_REQUIRE(std::same_as<decltype(val), const int &>);
			}

			THEN("not assignable") {
				STATIC_REQUIRE_FALSE(std::assignable_from<decltype(val), int>);
			}
		}
	}

	GIVEN("a non-const rvalue Optional<int>") {
		WHEN("dereferenced") {
			auto &&val = *AK::Optional<int>{77};

			THEN("type is int&&") {
				STATIC_REQUIRE(std::same_as<decltype(val), int &&>);
			}

			THEN("not assignable") {
				STATIC_REQUIRE_FALSE(std::is_assignable_v<decltype(val), int>);
			}
		}
	}

	GIVEN("a const rvalue Optional<int>") {
		const AK::Optional<int> co{88};

		WHEN("dereferenced") {
			auto &&val = std::move(*co);

			THEN("type is const int&") {
				STATIC_REQUIRE(std::same_as<decltype(val), const int &&>);
			}

			THEN("not assignable") {
				STATIC_REQUIRE_FALSE(std::assignable_from<decltype(val), int>);
			}
		}
	}
}
