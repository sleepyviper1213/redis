# Code patterns

The idioms new code uses, each with the reason for it, so the same problem is solved the same way everywhere. The
[SDD §9](software_design_document.md#9-patterns-viewpoint) names the architectural patterns; this page is the level
below, the shape of a function or a type. Several come from SerenityOS's
[AK](https://github.com/SerenityOS/serenity/blob/master/Documentation/Patterns.md), adapted to standard C++ and to
[ADR-0009](adr/0009-errors-as-values.md) (errors as values).

A pattern marked *planned* is decided but not yet in the code; its section says which branch brings it.

## Errors

| Spelling | Use for | Not for |
|----------|---------|---------|
| `std::expected<T, Failure<Kind>>` | Anything that can fail for a reason outside the program: a file, a socket, a client's bytes | A broken invariant |
| `REDIS_FAILED(error)` | `return std::unexpected{error}`, with a braced `Failure` passing through as one argument | |
| `REDIS_TRY(expr)` | Pass a failure up unchanged; the value otherwise | MSVC (a GNU statement expression) |
| `REDIS_TRY_FROM(expr, E2)` | Pass a failure up as the function's own error type, through `E2`'s constructor or `From<E1, E2>` | |
| `REDIS_MUST(expr)`, `REDIS_EXPECT(expr, why)` | An `expected` that cannot hold an error here (the input was just validated): abort with the error's text | A failure that can happen: that is `REDIS_TRY` |
| `REDIS_VERIFY(cond, why)` | An invariant the program cannot continue without; stays on in release builds and ends in the crash report | Input validation |

Wording lives in one place per module, `format_as` or `display()` of the `Failure<Kind>`, so a log line, a startup
message and an `ERR` reply say the same thing ([SDD §14.5](software_design_document.md#145-error-handling)). Error
paths do not allocate their message: the kind and the fields carry the facts, and the text is built when printed.

## Fallible construction: `create()`

A constructor cannot return an error, so a type whose setup can fail would either throw or be built half-ready and
finished by a second call (`listen()`, `load()`). Instead it has a private constructor that cannot fail and a static
factory that does the fallible part first:

```cpp
class DurableFile {
public:
    [[nodiscard]] static std::expected<DurableFile, std::error_code>
    open_existing(const std::filesystem::path &file, Access access);
private:
    explicit DurableFile(Descriptor fd) noexcept;   // cannot fail
};
```

A value that exists is ready to use: there is no "opened" flag to check and no method that is only valid after
another. When the type cannot move (it holds a mutex, an `io_context`, a `counting_scope`), `create()` returns
`std::expected<std::unique_ptr<T>, E>`, and the pointer is never null on success. Name the factory after what it does
when that says more (`open_existing`, `listen`); `create` otherwise.

In the code: `os::DurableFile`, `os::SaveFile`; `Listener::create`, which binds before the listener exists, and
`RedisServer::create`, which turns what Asio throws during setup into `ServerKind::SetupFailed` (DX-94). Their
constructors take a private `Token`, so `std::make_unique` works inside `create()` and nowhere else.

## Checked arithmetic: `Checked<T>`

An integer that remembers whether any step that produced it overflowed, after AK's
`Checked<T>`, over the `__builtin_*_overflow` helpers of `saturating.hpp`:

```cpp
auto millis = Checked<int64_t>(amount);
if (unit == ExpireUnit::Seconds) millis *= MS_PER_S;
if (base == ExpireBase::Relative) millis += now_ms;
if (millis.has_overflow()) return std::nullopt;   // "invalid expire time"
return DeadlineTimer::at_ms(millis.value());
```

The overflow is checked once, after the whole computation, instead of after each step through an out-parameter.
`value()` on an overflowed `Checked` is a `REDIS_VERIFY` failure, never a wrapped number. Use it where Redis answers
an overflow with an error (`INCRBY`, `EXPIRE`); use the saturating functions where clamping is the behaviour
(`DeadlineTimer`, `DateTime`).

In the code: `core/utils/checked.hpp`; `INCRBY` and `DECRBY` (`commands/string.cpp`), the expiry arguments
(`commands/detail/expire_time.hpp`).

## Access tokens: `Badge<T>`

A public method that only one class may call takes a `Badge<Caller>` first. Only `Caller` can make one (its
constructor is private and `Caller` is a friend), so the call compiles only there, and the rest of the class stays
private. Better than `friend class Caller`, which opens every member. A badge is passed as `{}` and cannot be copied,
so a callee cannot pass it on.

```cpp
class AppendOnlyFile {
public:
    // The dispatcher's: a command calling it would log itself twice.
    void command_ran(Badge<CommandHandler>, Database &db, bool is_logged, Args args);
};

ctx.aof->command_ran({}, ctx.db, is_logged, args);   // in a CommandHandler member
```

In the code: `core/utils/badge.hpp`; `AppendOnlyFile::command_starting` and `command_ran`, `ServerStats::command_ran`,
all called once per command by `CommandHandler`. A type-local variant, a private `Token` struct, keeps the
constructors of types made by `create()` to `create()` alone.

## Strong types

*Planned* (`test/strong-types`). A quantity is a type with its unit, not a bare integer, after
[mp-units](https://github.com/mpusage/mp-units): byte sizes, ports, database indexes and counts cannot be mixed up or
passed in the wrong order, and a conversion between units is written once. Durations already work this way
(`std::chrono`); the rest follows its model: a value type with explicit construction, the arithmetic that makes sense
for the dimension (a size plus a size is a size; a size times a size does not compile), and named units
(`64 * MiB`).

A `using Port = uint16_t` alias is not a strong type: it documents and checks nothing.

Until then, `core/size_units.hpp` has `KiB` and `MiB` as plain `std::size_t` constants, so a byte count reads as
`64 * KiB` and not `64 * 1024`. They check nothing; they only name the unit.

## Scope cleanup: `ScopeGuard`, `ScopedValueRollback`, owning handles

Work that must happen on every way out of a scope (return, early exit, exception) goes in a destructor, not after
each exit. From SerenityOS AK:

| Spelling | Use for |
|----------|---------|
| `ScopeGuard guard{&function}` | A call to make once at the end of the scope, such as clearing the crash report's current command |
| `ScopedValueRollback rollback{variable}` | A variable the scope changes and must put back; `set_rollback_value()` keeps a change that succeeded |
| A class holding a `std::unique_ptr<T, Releaser>` | A handle that is moved around and releases something once, such as `NetGuard::Ticket`: the moves and destructor are defaulted |

Neither guard can be copied or moved, and the function must be `noexcept`, as it runs in a destructor. Give
`ScopeGuard` a named function or lambda, not a long inline one. A handle keeps its `unique_ptr` private and its
constructor private to its maker. Used as a bare alias, `release()`, `reset()` and `get()` would let any caller
make or leak one.

## Intrusive containers

Not adopted yet: no container of clients or sessions exists. Sessions are tracked by an stdexec `counting_scope`,
which needs no list. When a feature needs one (`CLIENT LIST`, blocked clients, LRU eviction), use
[Boost.Intrusive](https://www.boost.org/doc/libs/release/doc/html/intrusive.html) rather than a `std::list` of
pointers: the node holds its own hook, so linking allocates nothing, and an `auto_unlink` hook removes a session
from every list when it is destroyed. AK's `IntrusiveList` does the same but is not usable outside AK.

## Platform code

Everything that calls the operating system directly is in `src/core/os/` (and the crash handler in
`src/server/diagnostics/`), behind an interface that is the same on every platform. The rest builds and is tested on
Windows, where the development happens, so CI only has to cover that thin layer on Linux and macOS. An `#ifdef` on a
platform outside those directories is a bug to move; `scripts/check_platform_code.sh` fails on one, in the static
analysis job. Where only one file differs, CMake picks the file (`chrono/tick_clock.cpp` or
`os/tick_clock_mach.cpp`) instead of an `#ifdef` choosing inside it.
