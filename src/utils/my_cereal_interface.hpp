#pragma once
#include <fstream>
#define MY_CEREAL_INTERACE 0

#ifdef MY_CEREAL_INTERACE
#include <cereal/archives/binary.hpp>

template <typename T>
void save(const T &var, const std::string &filename) {
	std::ofstream os(filename, std::ios::binary);
	cereal::BinaryOutputArchive archive(os);
	archive(var);
}

template <typename T>
T load(const std::string &filename) {
	std::ifstream is(filename, std::ios::binary);
	cereal::BinaryInputArchive archive(is);
	T var;
	archive(var);
	return var;
}
#endif
