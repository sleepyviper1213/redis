// Runs a fuzz harness over files, for builds without libFuzzer (DX-86).
//
//     redis_fuzz_resp_parser <file or directory>...
//
// Each file is one input to LLVMFuzzerTestOneInput(), as libFuzzer would pass
// it. A failing input aborts (sanitizer report or REDIS_VERIFY), so reaching
// the end means every input passed. Finding no input at all is an error: a
// mistyped corpus path must not pass silently.

#include <fmt/format.h>
#include <fmt/std.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

namespace {
namespace fs = std::filesystem;

/// Every regular file under \p root (or \p root itself), sorted so a run
/// replays in the same order on every machine.
std::vector<fs::path> collect_inputs(const fs::path &root) {
	std::error_code ec;
	if (!fs::is_directory(root, ec)) return {root};

	std::vector<fs::path> files;
	for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end;
		 it.increment(ec)) {
		if (it->is_regular_file(ec)) files.push_back(it->path());
	}
	if (ec) fmt::println(stderr, "cannot read {}: {}", root, ec.message());
	std::ranges::sort(files);
	return files;
}

bool replay(const fs::path &file) {
	std::ifstream in(file, std::ios::binary);
	if (!in) {
		fmt::println(stderr, "cannot open {}", file);
		return false;
	}
	const std::vector<char> bytes{std::istreambuf_iterator<char>(in),
								  std::istreambuf_iterator<char>()};
	// So a crash is attributed to its input even without a debugger.
	fmt::println(stderr, "replaying {}", file);
	LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t *>(bytes.data()),
						   bytes.size());
	return true;
}
} // namespace

int main(int argc, char **argv) {
	size_t replayed = 0;
	bool all_read   = true;
	for (const char *arg : std::span(argv, static_cast<size_t>(argc)).subspan(1)) {
		for (const fs::path &file : collect_inputs(arg)) {
			if (replay(file))
				++replayed;
			else
				all_read = false;
		}
	}
	fmt::println("replayed {} inputs", replayed);
	return all_read && replayed > 0 ? 0 : 1;
}
