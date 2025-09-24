#pragma once
#include <array>
#include <string>
#include <string_view>

class PerfectCommandMap {
public:
	const char *FindCommand(const std::string &cmd) {
		uint32_t idx = perfect_hash(cmd.data(), cmd.size());
		if (idx < command_count)
			return commands[idx]; // Array of static strings
		return nullptr;
	}

private:
	std::array<std::string_view, 3> commands = {"GET", "SET", "DEL"};
};
