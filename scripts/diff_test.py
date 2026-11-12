"""Differential test: one deterministic command stream to redis_server and to a
real Redis, reply bytes compared, the first divergence reported.

The stress test checks replies against a model this project wrote, so it
cannot catch a behaviour the model gets wrong the same way. Real Redis is the
specification here: wrong types, missing keys, odd ranges, option combinations
and error replies are all exercised, and a byte that differs is a bug in one of
the two (almost always ours).

Replies are cut out of the socket stream by hand, not through a client
library, because the bytes are what is compared: a client would print a nil
bulk string ($-1) and a nil array (*-1) the same way.

Requests go one at a time, so both servers hold the same keyspace until the
first divergence. Replies in an order Redis does not define (SMEMBERS, SINTER,
KEYS, HGETALL) are compared sorted. Nothing time-dependent is sent (no TTL
reads, no short expiries).

    python3 scripts/diff_test.py --port 6380 --reference-port 6379 [--seed n]

scripts/diff_server.sh starts both servers. A failing run prints its seed and
the requests before the divergence; rerun with --seed to replay.
"""
import argparse
import collections
import random
import socket
import sys

HISTORY = 10
MAX_REPORTED = 20
KEYS = 6
FAMILIES = "slth"  # string, list, set ("t"), hash


class Failure(Exception):
    pass


def printable(data, limit=200):
    out = []
    for b in data[:limit]:
        if b == 13:
            out.append("\\r")
        elif b == 10:
            out.append("\\n")
        elif 32 <= b < 127:
            out.append(chr(b))
        else:
            out.append("\\x%02x" % b)
    if len(data) > limit:
        out.append("...(%d bytes)" % len(data))
    return "".join(out)


def encode(args):
    out = [b"*%d\r\n" % len(args)]
    for a in args:
        out.append(b"$%d\r\n%s\r\n" % (len(a), a))
    return b"".join(out)


# ---------------------------------------------------------------------------
# RESP2 framing
# ---------------------------------------------------------------------------

class Incomplete(Exception):
    """More bytes are needed."""


def _length(buf, start, end):
    text = buf[start:end]
    try:
        return int(text)
    except ValueError:
        raise Failure("bad length in a reply: %s" % printable(text)) from None


def frame(buf, pos):
    """The end of the reply starting at pos. Raises Incomplete if it is not
    all there, Failure if the bytes are not RESP2."""
    if pos >= len(buf):
        raise Incomplete
    eol = buf.find(b"\r\n", pos)
    if eol < 0:
        raise Incomplete
    kind = buf[pos:pos + 1]
    if kind in (b"+", b"-", b":"):
        return eol + 2
    if kind == b"$":
        n = _length(buf, pos + 1, eol)
        if n < 0:
            return eol + 2
        end = eol + 2 + n + 2
        if end > len(buf):
            raise Incomplete
        if buf[end - 2:end] != b"\r\n":
            raise Failure("bulk string not followed by CRLF")
        return end
    if kind == b"*":
        n = _length(buf, pos + 1, eol)
        at = eol + 2
        for _ in range(max(n, 0)):
            at = frame(buf, at)
        return at
    raise Failure("not a RESP2 reply: %s" % printable(buf[pos:pos + 16]))


def elements(reply):
    """An array reply's elements, each as its own bytes; None if not an
    array."""
    if not reply.startswith(b"*"):
        return None
    eol = reply.find(b"\r\n")
    n = _length(reply, 1, eol)
    if n < 0:
        return None
    out, at = [], eol + 2
    for _ in range(n):
        nxt = frame(reply, at)
        out.append(reply[at:nxt])
        at = nxt
    return out


DEFINED, UNORDERED, PAIRS = range(3)


def canonical(reply, order):
    """reply with an undefined element order made canonical."""
    if order == DEFINED:
        return reply
    items = elements(reply)
    if items is None:
        return reply
    count = len(items)
    if order == PAIRS:
        items = [b"".join(items[i:i + 2]) for i in range(0, count, 2)]
    return b"*%d\r\n" % count + b"".join(sorted(items))


def error_code(reply):
    """An error reply cut to its code: -WRONGTYPE ... to -WRONGTYPE."""
    if not reply.startswith(b"-"):
        return reply
    return reply.split(b"\r\n", 1)[0].split(b" ", 1)[0]


class Connection:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port))
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""

    def call(self, request):
        """Send request and return its reply's bytes."""
        self.sock.sendall(request)
        while True:
            try:
                end = frame(self.buf, 0)
            except Incomplete:
                chunk = self.sock.recv(65536)
                if not chunk:
                    raise Failure("the server closed the connection") from None
                self.buf += chunk
                continue
            reply, self.buf = self.buf[:end], self.buf[end:]
            return reply


# ---------------------------------------------------------------------------
# The command stream: random requests over a small keyspace, so keys are often
# missing, often of the wrong type, and lists and sets grow and empty again.
# ---------------------------------------------------------------------------

class Generator:
    def __init__(self, seed):
        self.rng = random.Random(seed)

    def pick(self, n):
        return self.rng.randrange(n)

    def key(self, family):
        """Mostly a key of the family's own type, sometimes any, so wrong
        types come up."""
        if self.pick(8) == 0:
            family = FAMILIES[self.pick(len(FAMILIES))]
        return b"%s%d" % (family.encode(), self.pick(KEYS))

    def any_key(self):
        return self.key(FAMILIES[self.pick(len(FAMILIES))])

    def value(self):
        roll = self.pick(6)
        if roll == 0:
            return b"%d" % (self.pick(2001) - 1000)
        if roll == 1:
            return b""
        if roll == 2:
            return b"%d.5" % self.pick(10)
        if roll == 3:
            return b"9223372036854775807"
        return bytes(self.rng.choice(b"abcdefghijklmnopqrstuvwxyz")
                     for _ in range(self.pick(12)))

    def member(self):
        return b"m%d" % self.pick(10)

    def index(self):
        """An index or count: in range, past it, negative, or not a number."""
        if self.pick(25) == 0:
            return b"x"
        return b"%d" % (self.pick(17) - 8)

    def next(self):
        return (self.string, self.list, self.set, self.hash,
                self.keyspace)[self.pick(5)]()

    def string(self):
        k = self.key("s")
        roll = self.pick(14)
        if roll == 0:
            args = [b"SET", k, self.value()]
            # Options in random combinations, invalid ones included. Long
            # expiries only: the replies must not depend on timing.
            for _ in range(self.pick(3)):
                option = self.rng.choice(
                    [b"NX", b"XX", b"GET", b"KEEPTTL", b"EX", b"PX"])
                args.append(option)
                if option == b"EX":
                    args.append(b"100000" if self.pick(10) else b"0")
                elif option == b"PX":
                    args.append(b"100000000" if self.pick(10) else b"-5")
            return args, DEFINED
        return ([
            None,
            [b"GET", k],
            [b"GET", k],
            [b"APPEND", k, self.value()],
            [b"STRLEN", k],
            [b"INCR", k],
            [b"DECR", k],
            [b"INCRBY", k, self.index()],
            [b"DECRBY", k, self.index()],
            [b"MSET", k, self.value(), self.key("s"), self.value()],
            [b"MGET", k, self.key("s"), self.key("s")],
            [b"SET", k, self.value()],
            [b"GET", k, b"extra"],  # wrong arity
            [b"ECHO", self.value()],
        ][roll], DEFINED)

    def list(self):
        k = self.key("l")
        return ([
            [b"LPUSH", k, self.value(), self.value()],
            [b"RPUSH", k, self.value()],
            [b"LPOP", k],
            [b"RPOP", k],
            [b"LPOP", k, self.index()],  # with a count
            [b"RPOP", k, self.index()],
            [b"LLEN", k],
            [b"LINDEX", k, self.index()],
            [b"LRANGE", k, self.index(), self.index()],
            [b"LRANGE", k, self.index(), self.index()],
        ][self.pick(10)], DEFINED)

    def set(self):
        k = self.key("t")
        roll = self.pick(7)
        if roll == 5:
            return [b"SMEMBERS", k], UNORDERED
        if roll == 6:
            return [b"SINTER", k, self.key("t")], UNORDERED
        return ([
            [b"SADD", k, self.member(), self.member()],
            [b"SADD", k, self.member()],
            [b"SREM", k, self.member()],
            [b"SCARD", k],
            [b"SISMEMBER", k, self.member()],
        ][roll], DEFINED)

    def hash(self):
        k = self.key("h")
        roll = self.pick(8)
        if roll == 7:
            return [b"HGETALL", k], PAIRS
        return ([
            [b"HSET", k, self.member(), self.value()],
            [b"HSET", k, self.member(), self.value(), self.member(),
             self.value()],
            [b"HGET", k, self.member()],
            [b"HMGET", k, self.member(), self.member()],
            [b"HDEL", k, self.member()],
            [b"HLEN", k],
            [b"HEXISTS", k, self.member()],
        ][roll], DEFINED)

    def keyspace(self):
        roll = self.pick(9)
        if roll == 6:
            return [b"KEYS", b"*"], UNORDERED
        return ([
            [b"DEL", self.any_key(), self.any_key()],
            [b"EXISTS", self.any_key(), self.any_key()],
            [b"TYPE", self.any_key()],
            [b"EXPIRE", self.any_key(), b"100000"],
            [b"PERSIST", self.any_key()],
            [b"DBSIZE"],
            None,
            [b"PING"],
            [b"NOSUCHCOMMAND", self.any_key()],
        ][roll], DEFINED)


def show(args):
    return " ".join(printable(a, 40) if a else '""' for a in args)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("-p", "--port", type=int, default=6380,
                   help="redis_server's port")
    p.add_argument("--reference-port", type=int, default=6379,
                   help="the real Redis' port")
    p.add_argument("-n", "--ops", type=int, default=100000)
    p.add_argument("--seed", type=int, default=0,
                   help="RNG seed (0 picks one and prints it)")
    p.add_argument("--loose-errors", action="store_true",
                   help="compare error replies by their code only")
    p.add_argument("--keep-going", action="store_true",
                   help="report every divergence, then a count per command")
    opt = p.parse_args()
    if opt.seed == 0:
        opt.seed = random.SystemRandom().randrange(1, 2**32)
    print("diff: %d requests, seed %d, ours on %d, reference on %d"
          % (opt.ops, opt.seed, opt.port, opt.reference_port), flush=True)

    try:
        ours = Connection(opt.host, opt.port)
        reference = Connection(opt.host, opt.reference_port)
        flush = encode([b"FLUSHDB"])
        if ours.call(flush) != reference.call(flush):
            raise Failure("FLUSHDB was answered differently")

        gen = Generator(opt.seed)
        history = collections.deque(maxlen=HISTORY)
        per_command = collections.Counter()
        diverged = 0
        for i in range(1, opt.ops + 1):
            args, order = gen.next()
            wire = encode(args)
            got = canonical(ours.call(wire), order)
            want = canonical(reference.call(wire), order)
            same = (error_code(got) == error_code(want) if opt.loose_errors
                    else got == want)
            if not same:
                diverged += 1
                per_command[args[0].decode()] += 1
                if diverged <= MAX_REPORTED:
                    before = "".join("    %s\n" % h for h in history)
                    print("\nDIVERGED at request #%d: %s\n"
                          "  redis_server: %s\n"
                          "  Redis:        %s\n"
                          "  the requests before it (oldest first):\n%s"
                          "  replay with --seed %d"
                          % (i, show(args), printable(got), printable(want),
                             before, opt.seed), file=sys.stderr)
                # The keyspaces may differ from here on, so what follows can
                # diverge only because of this one.
                if not opt.keep_going:
                    return 1
            history.append("#%d %s -> %s" % (i, show(args),
                                             printable(want, 60)))
        if diverged:
            print("\n%d divergences; per command:" % diverged, file=sys.stderr)
            for command, n in sorted(per_command.items()):
                print("  %-12s %d" % (command, n), file=sys.stderr)
            return 1
    except (Failure, OSError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 2
    print("PASS: %d requests, every reply identical" % opt.ops)
    return 0


if __name__ == "__main__":
    sys.exit(main())
