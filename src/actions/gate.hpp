#pragma once
#include "actions.hpp"
#include "error.hpp"
#include "primitive/ret.hpp"

#include <expected>
#include <memory>

class Gate {
public:
	ErrorOr<Ret> parseAndExecute(const std::string &cmd);

private:
	std::unique_ptr<Db> db;
	std::unique_ptr<Snapshot> snapshot;
};
