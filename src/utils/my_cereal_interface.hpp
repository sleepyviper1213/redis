#pragma once
#include "error/error_or.hpp"

#include <cereal/archives/binary.hpp>

#include <concepts>
#include <fstream>
#include <system_error>

template <typename T>
void save(const T &var, const std::filesystem::path &filename) {
	std::ofstream os(filename, std::ios::binary);
	cereal::BinaryOutputArchive archive(os);
	archive(var);
}

template <std::default_initializable T>
ErrorOr<T> load(const std::filesystem::path &filename) {
	std::ifstream is(filename, std::ios::in | std::ios::binary);
	if (!is.is_open())
		return failed("No binary file found",
					  std::errc::no_such_file_or_directory);
	try {
		cereal::BinaryInputArchive archive(is);
		T var;
		archive(var);
		return var;
	} catch (const cereal::Exception &e) {
		return failed(e.what(), std::errc::operation_canceled);
	}
}
