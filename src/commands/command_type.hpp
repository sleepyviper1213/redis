#pragma once
#include "client_connection_command.hpp"
#include "keyspace_command.hpp"
#include "list_command.hpp"
#include "server_command.hpp"
#include "set_command.hpp"
#include "string_command.hpp"

#include <optional>
#include <string_view>
#include <variant>

using CommandType =
	std::variant<KeyspaceCommand, ServerCommand, ClientConnectionCommand,
				 SetCommand, StringCommand, ListCommand>;
