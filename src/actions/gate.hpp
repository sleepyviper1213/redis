#pragma once
#include "actions.hpp"
#include "error.hpp"

#include <expected>

class Gate {
public:
	ErrorOr<Ret> parseAndExecute(const std::string &cmd);

private:
	Db db_;
	Snapshot snapshot_;
};

// TODO: this function should not be declared here
std::string format_as(const ErrorOr<Ret> &x);
