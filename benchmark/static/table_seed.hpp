#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <random>

class table_seed {
public:
	void next() {
		this->data += 1;
		this->data_256_byte_initd = false;
	}

	unsigned long get_32() const { return this->data; }

	const std::array<uint8_t, 256> &get_256_byte() const {
		if (!this->data_256_byte_initd) this->init_data_256_byte();
		return this->data_256_byte;
	}

private:
	void init_data_256_byte() const {
		std::ranges::iota(this->data_256_byte, 0);
		std::mt19937 rng(this->data);
		std::ranges::shuffle(this->data_256_byte, rng);
		this->data_256_byte_initd = true;
	}

	unsigned long data               = 0x811c'9dc5; // FNV-1a 32-bit basis.
	mutable bool data_256_byte_initd = false;
	mutable std::array<uint8_t, 256> data_256_byte;
};
