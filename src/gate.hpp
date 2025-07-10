#pragma once
#include "db.hpp"
#include "error/app_error.hpp"
#include "snapshot.hpp"

#include <expected>
#include <memory>

class Gate {
public:
	std::expected<Ret, AppError> parseAndExecute(const std::string &cmd);

private:
	std::unique_ptr<Db> db;
	std::unique_ptr<Snapshot> snapshot;
};
