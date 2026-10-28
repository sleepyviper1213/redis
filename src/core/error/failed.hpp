#pragma once

#define FAILED(error)                                                          \
	return std::unexpected { error }
