#pragma once
// Proof that the caller is a T, after SerenityOS's AK::Badge
// (docs/design/patterns.md).
//
// A public method that only T may call takes a Badge<T> first. Only T can make
// one (the constructor is private and T a friend), so a call from anywhere
// else does not compile, and the rest of the class stays private: `friend
// class T` would open every member to T.
//
//     class AppendOnlyFile {
//     public:
//         void command_starting(Badge<CommandHandler>) noexcept;
//     };
//     // in a CommandHandler member function:
//     ctx.aof->command_starting({});
//
// It carries nothing, is passed by value as `{}`, and cannot be copied or
// kept: a callee cannot hand it on.

namespace redis {

template <typename T>
class Badge {
public:
	Badge(const Badge &)            = delete;
	Badge &operator=(const Badge &) = delete;
	Badge(Badge &&)                 = delete;
	Badge &operator=(Badge &&)      = delete;
	~Badge()                        = default;

private:
	friend T;
	constexpr Badge() noexcept = default;
};

} // namespace redis
