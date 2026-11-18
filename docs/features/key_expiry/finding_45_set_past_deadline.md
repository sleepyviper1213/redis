# Finding: SET with an already-past EXAT or PXAT (#45)

Spike, 2 hours. Question: does Redis keep a key set with an already-past `EXAT`/`PXAT` until it expires, as the
`DBSIZE` difference in the differential test suggests?

**Answer: yes.** Redis stores the key with its past deadline and removes it lazily. This differs from the `EXPIRE`
family, which deletes at once. The specification (FR-006.5, DX-23, UC-011 A4) says the key is deleted at once for every
command, which is wrong for `SET`.

## Evidence

Redis in CI is 8.0.2 (Debian 13 package `5:8.0.2-3+deb13u3`, CI run 52, job `x86_64-linux-gnu-gcc-release`). Read in the
source at tag 8.0.2:

| Command | Past deadline | Source |
|---------|---------------|--------|
| `SET k v EXAT t`, `PXAT t` (t in the past) | Stores the key and sets the deadline; replies `+OK`; no already-expired check. Propagated as `SET k v PXAT t` | `t_string.c`, `setGenericCommand` |
| `SET k v EX n`, `PX n` with `n <= 0` | Error `invalid expire time in 'set' command` | `t_string.c`, `getExpireMillisecondsOrReply` |
| `EXPIRE`, `PEXPIRE`, `EXPIREAT`, `PEXPIREAT` | Deletes the key at once; replies `:1`; propagated as `DEL` | `expire.c`, `expireGenericCommand` via `checkAlreadyExpired` |
| `GETEX k EXAT t`, `PXAT t` | Replies with the value, then deletes the key; propagated as `DEL` | `t_string.c`, `getexCommand` |

`checkAlreadyExpired` is false while loading or on a replica, which then keep the key for the same reason: a replica
waits for the master's `DEL`.

Observed in the differential test (run 52, seed `1705924782`, request #2182): after `#2174 SET t3 "" PXAT 1 -> +OK`,
`#2175 KEYS *` gave the same 16 keys in both servers, and `#2182 DBSIZE` was `:16` here and `:17` in Redis. That
matches the source: `KEYS *` skips a due key, `DBSIZE` counts it. The run reported 92 `DBSIZE` divergences. Divergences
are mostly `EXPIRE` ones (193 of 285); those I read are `EXPIRE k 10 11`, answered `Unsupported option` by Redis (#41).

## Answers to the questions of #45

1. **What does Redis do?** It depends on the command: `SET` with `EXAT`/`PXAT` keeps the key until it is read or swept;
   the `EXPIRE` family and `GETEX` delete at once. `SET` with `EX`/`PX` and a zero or negative amount is an error.
2. **Visible beyond `DBSIZE`?** Yes, by the source (only `DBSIZE` was observed): `INFO keyspace` (`keys=`, `expires=`) counts the key; `expired_keys` rises only
   when the key is read or swept; the log and replicas receive `SET k v PXAT t` first and a `DEL` later, where this
   server logs a `DEL` at once (TC-113). `EXISTS`, `GET`, `TTL` and `KEYS` do not see the key. There is no
   replication here, so only the log differs.
3. **Follow it or document it?** Open decision, see below.

## Decision

Follow Redis, decided in the session of 18 Nov 2026. Tracked in #48. The spec is changed first (FR-006.5, SDD §13.1, DX-23,
UC-011 A4, UC-002 AC-03, UC-021, the state diagram, TC-113, TC-263), then the code. The differential step can become
gating once #41 is dealt with.

Related: LI-05, #30, #41, #43.
