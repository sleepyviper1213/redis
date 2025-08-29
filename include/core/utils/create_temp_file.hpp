#pragma once
#include <filesystem>

namespace redis {

/**
 * \brief Utility function to create a temporary file.
 */
std::filesystem::path createTempFile();
} // namespace redis