/* C++ code produced by gperf version 3.3 */
/* Command-line: gperf --output-file=redis_cmds.hpp redis_commands.gperf  */
/* Computed positions: -k'1-3' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gperf@gnu.org>."
#endif

#line 8 "redis_commands.gperf"

#include "resp/value.hpp"
#include "memory/database.hpp"

#include <string_view>
#include "commands/crud.hpp"
#include "commands/exists.hpp"
#include "commands/list.hpp"
#include "commands/ping.hpp"
#include "commands/ttl.hpp"

#include <cstddef>
#include <cstring>
#include <string_view>

namespace redis {
#line 25 "redis_commands.gperf"
struct CommandInfo {
	using handle_func = resp::Value (*)(Database &, const resp::Value::Array &);

	const char* name;
	handle_func func; // Function to execute
	int arity;        // Required arity (negative = min args)
};
/* maximum key range = 211, duplicates = 0 */

class gperf
{
private:
  static inline unsigned int hash (const char *str, size_t len);
public:
  static const struct CommandInfo *in_word_set (const char *str, size_t len);
};

inline unsigned int
gperf::hash (const char *str, size_t len)
{
  static const unsigned char asso_values[] =
    {
      215, 215, 215, 215, 215, 215, 215, 215, 215, 215,
      215, 215, 215, 215, 215, 215, 215, 215, 215, 215,
      215, 215, 215, 215, 215, 215, 215, 215, 215, 215,
      215, 215, 215, 215, 215, 215, 215, 215, 215, 215,
      215, 215, 215, 215, 215, 215, 215, 215, 215, 215,
      215, 215, 215, 215, 215, 215, 215, 215, 215, 215,
      215, 215, 215, 215, 215,   0,  25,  10,  75,   0,
        5,  40, 125,  50, 215,   0,  15,  75,   0,  65,
       65,  40,   0,  25,   0,  20,  70, 215,  50,   0,
       45, 215, 215, 215, 215, 215, 215, 215, 215, 215,
      215, 215, 215, 215, 215, 215, 215, 215, 215, 215,
      215, 215, 215, 215, 215, 215, 215, 215, 215, 215,
      215, 215, 215, 215, 215, 215, 215, 215
    };
  return len + asso_values[static_cast<unsigned char>(str[2])] + asso_values[static_cast<unsigned char>(str[1])] + asso_values[static_cast<unsigned char>(str[0])];
}

const struct CommandInfo *
gperf::in_word_set (const char *str, size_t len)
{
  enum
    {
      TOTAL_KEYWORDS = 78,
      MIN_WORD_LENGTH = 3,
      MAX_WORD_LENGTH = 16,
      MIN_HASH_VALUE = 4,
      MAX_HASH_VALUE = 214
    };

#if (defined __GNUC__ && __GNUC__ + (__GNUC_MINOR__ >= 6) > 4) || (defined __clang__ && __clang_major__ >= 3)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
  static const struct CommandInfo wordlist[] =
    {
      {""}, {""}, {""}, {""},
#line 42 "redis_commands.gperf"
      {"KEYS", 0},
      {""},
#line 109 "redis_commands.gperf"
      {"RENAME", 0},
      {""},
#line 110 "redis_commands.gperf"
      {"RENAMENX", 0},
#line 107 "redis_commands.gperf"
      {"RANDOMKEY", 0},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
#line 41 "redis_commands.gperf"
      {"TTL", handle_ttl, 2},
      {""},
#line 64 "redis_commands.gperf"
      {"LTRIM", 0},
#line 63 "redis_commands.gperf"
      {"LRANGE", 0},
      {""}, {""},
#line 89 "redis_commands.gperf"
      {"AUTH", 0},
      {""}, {""}, {""},
#line 35 "redis_commands.gperf"
      {"SET", handle_set, 3},
#line 52 "redis_commands.gperf"
      {"SREM", 0},
      {""},
#line 103 "redis_commands.gperf"
      {"STRLEN", 0},
#line 97 "redis_commands.gperf"
      {"RESTORE", 0},
      {""},
#line 62 "redis_commands.gperf"
      {"LLEN", 0},
      {""},
#line 56 "redis_commands.gperf"
      {"SRANDMEMBER", 0},
      {""}, {""},
#line 43 "redis_commands.gperf"
      {"SCAN", 0},
#line 54 "redis_commands.gperf"
      {"SCARD", 0},
#line 93 "redis_commands.gperf"
      {"SCRIPT", 0},
      {""},
#line 34 "redis_commands.gperf"
      {"GET", handle_get, 2},
#line 65 "redis_commands.gperf"
      {"LSET", 0},
#line 95 "redis_commands.gperf"
      {"FLUSH", 0},
#line 90 "redis_commands.gperf"
      {"SELECT", 0},
#line 82 "redis_commands.gperf"
      {"FLUSHDB", 0},
#line 83 "redis_commands.gperf"
      {"FLUSHALL", 0},
#line 70 "redis_commands.gperf"
      {"ZREM", 0},
      {""},
#line 74 "redis_commands.gperf"
      {"ZRANGE", 0},
      {""}, {""},
#line 75 "redis_commands.gperf"
      {"ZREVRANGE", 0},
      {""},
#line 79 "redis_commands.gperf"
      {"UNSUBSCRIBE", 0},
      {""}, {""},
#line 86 "redis_commands.gperf"
      {"INFO", 0},
#line 73 "redis_commands.gperf"
      {"ZCARD", 0},
#line 76 "redis_commands.gperf"
      {"ZREMRANGEBYSCORE", 0},
      {""}, {""},
#line 38 "redis_commands.gperf"
      {"INCR", 0},
      {""},
#line 100 "redis_commands.gperf"
      {"INCRBY", 0},
      {""}, {""},
#line 98 "redis_commands.gperf"
      {"PTTL", handle_pttl, 2},
      {""},
#line 66 "redis_commands.gperf"
      {"LINDEX", 0},
      {""}, {""},
#line 91 "redis_commands.gperf"
      {"EVAL", 0},
      {""}, {""},
#line 92 "redis_commands.gperf"
      {"EVALSHA", 0},
      {""},
#line 78 "redis_commands.gperf"
      {"SUBSCRIBE", 0},
      {""},
#line 87 "redis_commands.gperf"
      {"CLIENT", 0},
      {""}, {""},
#line 94 "redis_commands.gperf"
      {"LOAD", 0},
      {""},
#line 71 "redis_commands.gperf"
      {"ZSCORE", 0},
      {""}, {""},
#line 39 "redis_commands.gperf"
      {"DECR", 0},
#line 59 "redis_commands.gperf"
      {"RPUSH", handle_list, 3},
#line 101 "redis_commands.gperf"
      {"DECRBY", 0},
      {""},
#line 36 "redis_commands.gperf"
      {"DEL", handle_del, 2},
#line 111 "redis_commands.gperf"
      {"SORT", 0},
#line 68 "redis_commands.gperf"
      {"BRPOP", 0},
#line 85 "redis_commands.gperf"
      {"BGSAVE", 0},
#line 81 "redis_commands.gperf"
      {"PUNSUBSCRIBE", 0},
      {""},
#line 84 "redis_commands.gperf"
      {"SAVE", 0},
      {""}, {""},
#line 72 "redis_commands.gperf"
      {"ZINCRBY", 0},
      {""},
#line 51 "redis_commands.gperf"
      {"SADD", 0},
#line 58 "redis_commands.gperf"
      {"LPUSH", 0},
#line 37 "redis_commands.gperf"
      {"EXISTS", handle_exists, 2},
      {""},
#line 53 "redis_commands.gperf"
      {"SMEMBERS", 0},
#line 55 "redis_commands.gperf"
      {"SISMEMBER", 0},
#line 67 "redis_commands.gperf"
      {"BLPOP", 0},
      {""}, {""}, {""},
#line 88 "redis_commands.gperf"
      {"QUIT", 0},
      {""}, {""},
#line 77 "redis_commands.gperf"
      {"PUBLISH", 0},
      {""},
#line 105 "redis_commands.gperf"
      {"PING", handle_ping, 1},
#line 80 "redis_commands.gperf"
      {"PSUBSCRIBE", 0},
#line 40 "redis_commands.gperf"
      {"EXPIRE", 0},
#line 99 "redis_commands.gperf"
      {"PEXPIRE", 0},
      {""},
#line 69 "redis_commands.gperf"
      {"ZADD", 0},
      {""}, {""}, {""}, {""},
#line 106 "redis_commands.gperf"
      {"TIME", 0},
#line 48 "redis_commands.gperf"
      {"HKEYS", 0},
      {""}, {""}, {""},
#line 61 "redis_commands.gperf"
      {"RPOP", 0},
      {""},
#line 102 "redis_commands.gperf"
      {"APPEND", 0},
      {""}, {""},
#line 104 "redis_commands.gperf"
      {"ECHO", 0},
      {""}, {""}, {""}, {""},
#line 47 "redis_commands.gperf"
      {"HLEN", 0},
      {""}, {""}, {""}, {""},
#line 60 "redis_commands.gperf"
      {"LPOP", 0},
      {""}, {""}, {""}, {""},
#line 44 "redis_commands.gperf"
      {"HSET", 0},
      {""}, {""}, {""}, {""},
#line 57 "redis_commands.gperf"
      {"SPOP", 0},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
#line 45 "redis_commands.gperf"
      {"HGET", 0},
      {""}, {""},
#line 50 "redis_commands.gperf"
      {"HGETALL", 0},
      {""},
#line 96 "redis_commands.gperf"
      {"DUMP", 0},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""}, {""}, {""}, {""}, {""}, {""}, {""},
#line 49 "redis_commands.gperf"
      {"HVALS", 0},
      {""}, {""}, {""},
#line 46 "redis_commands.gperf"
      {"HDEL", 0},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
#line 108 "redis_commands.gperf"
      {"MOVE", 0}
    };
#if (defined __GNUC__ && __GNUC__ + (__GNUC_MINOR__ >= 6) > 4) || (defined __clang__ && __clang_major__ >= 3)
#pragma GCC diagnostic pop
#endif

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      unsigned int key = hash (str, len);

      if (key <= MAX_HASH_VALUE)
        {
          const char *s = wordlist[key].name;

          if (*str == *s && !strncmp (str + 1, s + 1, len - 1) && s[len] == '\0')
            return &wordlist[key];
        }
    }
  return static_cast<struct CommandInfo *> (0);
}
#line 112 "redis_commands.gperf"


constexpr bool is_redis_command(std::string_view cmd) noexcept {
	return gperf::in_word_set(cmd.data(), cmd.size()) != nullptr;
}
} // namespace redis 
