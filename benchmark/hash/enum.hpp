#pragma once
enum class hash_strategy {
	fnv1a32,
	xx3_64,
	crc32,
	lehmer64,
	lehmer_128,
	pearson_8,
};