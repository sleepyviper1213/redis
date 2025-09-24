#pragma once
#include "../prng/lehmer_engine.hpp"
class lehmer_hash{
	lehmer64_engine engine;
	uint64_t hash(uint32_t val) {
		const auto*x=reinterpret_cast<const char*>(&val);
//		 engine.seed();
	}
};