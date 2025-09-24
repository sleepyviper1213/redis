#include "hash.hpp"
#include "string_compare/strategies.hpp"

#include <absl/container/flat_hash_map.h>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_random.hpp>
#include <catch2/reporters/catch_reporter_console.hpp>
#include <catch2/reporters/catch_reporter_helpers.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

TEST_CASE("Hash function benchmarks", "[benchmark][dynamic][hash]") {
	constexpr std::string_view test_key =
		"The quick brown fox jumps over the lazy dog";

	BENCHMARK("fnv1a32")
	{ return Fnv1a32Hasher{}(test_key); };
	/*BENCHMARK("lehmer64")
	{ return lehmer64_hasher{}(test_key); };
	BENCHMARK("lehmer128")
	{ return lehmer128_hasher{}(test_key); };*/
	BENCHMARK("crc32")
	{ return crc32_hasher{0x8343}.hash(test_key); };
	BENCHMARK("aes")
	{ return aes_hash{}(test_key); };
	BENCHMARK("xxh3_64")
	{ return Xxh3_64Hasher{0x8343}.hash(test_key); };
	BENCHMARK("pearson64")
	{ return pearson64_hash{}.hash(test_key); };
}

// Helper function to generate test strings
std::pair<std::string, std::string>
generate_test_strings(size_t len, bool equal, size_t diff_pos = 0) {
	std::string a(len, 'A');
	std::string b = a;
	if (!equal && diff_pos < len) b[diff_pos] = 'B';
	return {a, b};
}

/*TEST_CASE("Benchmark String Comparison",
"[benchmark][dynamic][string_comparison]") {
	// Test cases: string lengths
	constexpr std::array<size_t, 4> lengths{8, 64, 100};

	for (size_t len : lengths) {
		SECTION("Equal strings, length " + std::to_string(len)) {
			auto [a, b] = generate_test_strings(len, true);
			for (auto &h : available_cmps) {
				BENCHMARK(h.name)
				{ return h.fn(a, b); };
			}
		}

		SECTION("Unequal strings (diff at start), length " +
				std::to_string(len)) {
			auto [a, b] = generate_test_strings(len, false, 0);
			for (auto &h : available_cmps) {
				BENCHMARK(h.name)
				{ return h.fn(a, b); };
			}
		}

		SECTION("Unequal strings (diff at middle), length " +
				std::to_string(len)) {
			auto [a, b] = generate_test_strings(len, false, len / 2);
			for (auto &h : available_cmps) {
				BENCHMARK(h.name)
				{ return h.fn(a, b); };
			}
		}

		SECTION("Unequal strings (diff at end), length " +
				std::to_string(len)) {
			auto [a, b] = generate_test_strings(len, false, len - 1);
			for (auto &h : available_cmps) {
				BENCHMARK(h.name)
				{ return h.fn(a, b); };
			}
		}
	}

	// Edge cases
	SECTION("Empty strings") {
		std::string a, b;
		for (auto &h : available_cmps) {
			BENCHMARK(h.name)
			{ return h.fn(a, b); };
		}
	}

	SECTION("Single-byte strings, equal") {
		std::string a = "A", b = "A";
		for (auto &h : available_cmps) {
			BENCHMARK(h.name)
			{ return h.fn(a, b); };
		}
	}

	SECTION("Single-byte strings, unequal") {
		std::string a = "A", b = "B";
		for (auto &h : available_cmps) {
			BENCHMARK(h.name)
			{ return h.fn(a, b); };
		}
	}
}
*/
// Helper: generate a random string of fixed length
std::string random_string(int length) {
	static constexpr std::string_view charset = "abcdefghijklmnopqrstuvwxyz"
												"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
												"0123456789";

	std::string s;
	s.reserve(length);

	for (int i = 0; i < length; ++i) {
		auto idx = Catch::Generators::random(0uz, charset.size() - 2).get();
		s.push_back(charset[idx]);
	}
	return s;
}

#define STR(x) #x

#define B(MAP, HASHER, CMP)                                                    \
	BENCHMARK_ADVANCED(#HASHER "-" #CMP)                                       \
	(Catch::Benchmark::Chronometer meter) {                                    \
		MAP<std::string, int, HASHER, CMP> a;                                  \
		a.reserve(values.size());                                              \
		meter.measure([&] {                                                    \
			for (auto &&i : values) a.insert_or_assign(i, 0);                  \
		});                                                                    \
	}
#define BENCH_ALL_HASHERS(MAP, CMP)                                            \
	B(MAP, Fnv1a32Hasher, CMP);                                                \
	B(MAP, crc32_hasher, CMP);                                                 \
	B(MAP, pearson64_hash, CMP);                                               \
	B(MAP, Xxh3_64Hasher, CMP);                                                \
	B(MAP, aes_hash, CMP)
// B(MAP, lehmer64_hasher, CMP);                                              \
// B(MAP, lehmer128_hasher, CMP);                                             \


// Arch-specific comparator expansion
#if defined(__aarch64__)
#define BENCH_ARCH_COMPARATORS(MAP) BENCH_ALL_HASHERS(MAP, CmpNeonMask)
#elif defined(__x86_64__)
#define BENCH_ARCH_COMPARATORS(MAP) BENCH_ALL_HASHERS(MAP, CmpPcmpistri)
#else
#define BENCH_ARCH_COMPARATORS(MAP)
#endif

// Sweep all comparators × all hashers for a MAP
#define BENCH_ALL(MAP)                                                         \
	SECTION(#MAP) {                                                            \
		BENCH_ALL_HASHERS(MAP, CmpMemcmp);                                     \
		BENCH_ALL_HASHERS(MAP, CmpCheckFirstThenMemcmp);                       \
		BENCH_ALL_HASHERS(MAP, CmpCheckTwoThenMemcmp);                         \
		BENCH_ARCH_COMPARATORS(MAP);                                           \
		B(std::unordered_map, std::hash<std::string>, std::equal_to<>);        \
		B(absl::flat_hash_map,                                                 \
		  absl::container_internal::StringHash,                                \
		  absl::container_internal::StringEq);                                 \
	}

TEST_CASE("Benchmark unordered_map", "[benchmark][dynamic][combinations]") {
	// Test cases: string lengths
	std::vector<std::string> values;
	values.reserve(1'000'000);
	for (int i = 0; i < 1'000'000; i++) values.push_back(random_string(128));
	BENCH_ALL(std::unordered_map);
	BENCH_ALL(absl::flat_hash_map);
}
