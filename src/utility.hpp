#pragma once
#include <cereal/archives/binary.hpp>

#include <fstream>
#include <vector>

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

std::vector<std::string> split_by_space(const std::string &input);
