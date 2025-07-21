#pragma once
#include "actions.hpp"
#include "error.hpp"

#include <expected>
#include <memory>


class Gate {
public:
	ErrorOr<Ret> parseAndExecute(const std::string &cmd);

private:
	std::unique_ptr<Db> db;
	std::unique_ptr<Snapshot> snapshot;
};

// TODO: this function should not be declared here
std::string format_as(const ErrorOr<Ret> &x);