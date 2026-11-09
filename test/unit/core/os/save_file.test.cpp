#include "support/temp_file.hpp"
#include "core/os/save_file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>

using namespace redis;
namespace fs = std::filesystem;

namespace {
std::string contents_of(const fs::path &file) {
	std::ifstream in(file, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

fs::path temp_beside(const fs::path &target) {
	return fs::path(target).concat(".new");
}
} // namespace

SCENARIO("A file is replaced as a whole", "[save_file]") {
	GIVEN("a file with content, and a new version being written") {
		test::TempFile target;
		const fs::path temp = temp_beside(target.path());
		std::ofstream(target.path(), std::ios::binary) << "old";

		// An optional, so that one section can drop it without a commit.
		std::optional<os::SaveFile> save;
		{
			auto opened = os::SaveFile::open(target.path(), temp);
			REQUIRE(opened.has_value());
			save.emplace(std::move(*opened));
		}
		REQUIRE(save->write("new ").has_value());
		save->stream() << "content";

		THEN("the target is still the old one until the commit") {
			CHECK(contents_of(target.path()) == "old");
			CHECK(fs::exists(temp));
		}

		WHEN("it is committed") {
			REQUIRE(save->commit().has_value());

			THEN("the target is the new content and the temporary is gone") {
				CHECK(contents_of(target.path()) == "new content");
				CHECK_FALSE(fs::exists(temp));
			}
		}

		WHEN("it is dropped without a commit") {
			save.reset();

			THEN("the target is untouched and the temporary is gone") {
				CHECK(contents_of(target.path()) == "old");
				CHECK_FALSE(fs::exists(temp));
			}
		}
	}
}

SCENARIO("A file is created if there was none", "[save_file]") {
	GIVEN("a target that does not exist") {
		test::TempFile target;
		auto save = os::SaveFile::open(target.path(),
												temp_beside(target.path()));
		REQUIRE(save.has_value());
		REQUIRE(save->write("fresh").has_value());

		WHEN("it is committed") {
			REQUIRE(save->commit().has_value());

			THEN("the target has the content") {
				CHECK(contents_of(target.path()) == "fresh");
			}
		}
	}
}

SCENARIO("More than the buffer holds is written whole", "[save_file]") {
	GIVEN("a few times the stream buffer's worth of data") {
		test::TempFile target;
		std::string big;
		for (int i = 0; i < 300'000; ++i) big += static_cast<char>('a' + i % 26);

		auto save = os::SaveFile::open(target.path(),
												temp_beside(target.path()));
		REQUIRE(save.has_value());

		WHEN("it is written in pieces of mixed sizes and committed") {
			save->stream() << "head:";
			save->stream().put('!');
			REQUIRE(save->write(big).has_value());
			save->stream().write(big.data(), 7);
			REQUIRE(save->commit().has_value());

			THEN("every byte arrives, in order") {
				CHECK(contents_of(target.path()) ==
					  "head:!" + big + big.substr(0, 7));
			}
		}
	}
}

SCENARIO("A leftover under the temporary's name is replaced", "[save_file]") {
	GIVEN("a stale temporary file from an earlier crash") {
		test::TempFile target;
		const fs::path temp = temp_beside(target.path());
		std::ofstream(temp, std::ios::binary) << "stale and much longer content";

		auto save = os::SaveFile::open(target.path(), temp);
		REQUIRE(save.has_value());
		REQUIRE(save->write("fresh").has_value());

		WHEN("it is committed") {
			REQUIRE(save->commit().has_value());

			THEN("none of the stale content is left") {
				CHECK(contents_of(target.path()) == "fresh");
			}
		}
	}
}

SCENARIO("A save that cannot start says which step failed", "[save_file]") {
	GIVEN("a temporary file in a directory that does not exist") {
		test::TempFile target;
		const fs::path temp = target.path().parent_path() / "no_such_dir_here" /
							  "temp";

		WHEN("the save is opened") {
			const auto save = os::SaveFile::open(target.path(), temp);

			THEN("it fails at create, with the system's reason") {
				REQUIRE_FALSE(save.has_value());
				CHECK(save.error().kind == os::SaveKind::Create);
				CHECK(save.error().cause);
				CHECK(save.error().path == target.path());
				CHECK(fmt::format("{}", save.error()).contains("create failed: "));
			}
		}
	}
}

SCENARIO("The temporary is not made through a link", "[save_file][security]") {
#ifdef _WIN32
	SKIP("symbolic links are not what they are on POSIX");
#else
	GIVEN("a link planted at the temporary's name, pointing at a file") {
		test::TempFile target;
		test::TempFile precious;
		std::ofstream(precious.path()) << "keep me";
		const fs::path temp = temp_beside(target.path());
		fs::create_symlink(precious.path(), temp);

		WHEN("a save is written and committed") {
			auto save = os::SaveFile::open(target.path(), temp);
			REQUIRE(save.has_value());
			REQUIRE(save->write("new").has_value());
			REQUIRE(save->commit().has_value());

			THEN("the file behind the link is untouched") {
				CHECK(contents_of(precious.path()) == "keep me");
				CHECK(contents_of(target.path()) == "new");
			}
		}
	}
#endif
}
