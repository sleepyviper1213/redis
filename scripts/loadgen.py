"""Pipelined load generator: compares redis_server binaries, alternating rounds."""
import argparse
import multiprocessing as mp
import os
import socket
import statistics
import subprocess
import time

PIPE = 64
VALUE = b"v" * 100
ELEM = b"e" * 20
LIST_LEN = 100
KEYS = 1000


def cmd(*parts):
    out = [b"*%d\r\n" % len(parts)]
    for p in parts:
        out.append(b"$%d\r\n%s\r\n" % (len(p), p))
    return b"".join(out)


def connect(port):
    s = socket.create_connection(("127.0.0.1", port))
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return s


def recv_exact(s, n, buf):
    view = memoryview(buf)[:n]
    got = 0
    while got < n:
        r = s.recv_into(view[got:], n - got)
        if not r:
            raise ConnectionError("closed")
        got += r


def workloads():
    get_reply = len(b"$%d\r\n" % len(VALUE)) + len(VALUE) + 2
    elem_reply = len(b"$%d\r\n" % len(ELEM)) + len(ELEM) + 2
    return {
        "GET 100B": (
            [b"".join(cmd(b"GET", b"key:%d" % ((b + i) % KEYS)) for i in range(PIPE))
             for b in range(0, KEYS, 7)],
            PIPE * get_reply),
        "SET 100B": (
            [b"".join(cmd(b"SET", b"key:%d" % ((b + i) % KEYS), VALUE) for i in range(PIPE))
             for b in range(0, KEYS, 7)],
            PIPE * 5),
        "LRANGE 100x20B": (
            [cmd(b"LRANGE", b"list", b"0", b"-1") * PIPE],
            PIPE * (len(b"*%d\r\n" % LIST_LEN) + LIST_LEN * elem_reply)),
    }


def worker(port, name, deadline, out):
    batches, reply_size = workloads()[name]
    s = connect(port)
    buf = bytearray(reply_size)
    done = 0
    i = 0
    while time.time() < deadline:
        s.sendall(batches[i % len(batches)])
        recv_exact(s, reply_size, buf)
        done += PIPE
        i += 1
    out.put(done)


def preload(port):
    s = connect(port)
    s.sendall(b"".join(cmd(b"SET", b"key:%d" % i, VALUE) for i in range(KEYS)))
    recv_exact(s, 5 * KEYS, bytearray(5 * KEYS))
    s.sendall(cmd(b"DEL", b"list") + cmd(b"RPUSH", b"list", *([ELEM] * LIST_LEN)))
    recv_exact(s, len(b":0\r\n") + len(b":%d\r\n" % LIST_LEN), bytearray(32))
    s.close()


def run_workload(port, name, seconds, procs):
    q = mp.Queue()
    deadline = time.time() + 1.0 + seconds
    ps = [mp.Process(target=worker, args=(port, name, deadline, q)) for _ in range(procs)]
    for p in ps:
        p.start()
    total = sum(q.get() for _ in ps)
    for p in ps:
        p.join()
    return total / (seconds + 1.0)


def with_server(exe, port, scratch, tag, fn):
    dump = os.path.join(scratch, f"load-{tag}.snap")
    if os.path.exists(dump):
        os.remove(dump)
    srv = subprocess.Popen([exe, "--port", str(port), "--dump", dump,
                            "--log-file", os.path.join(scratch, f"load-{tag}.log")],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try:
                connect(port).close()
                break
            except OSError:
                time.sleep(0.1)
        return fn()
    finally:
        srv.kill()
        srv.wait()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", action="append", required=True, help="label=path")
    ap.add_argument("--port", type=int, default=6391)
    ap.add_argument("--seconds", type=float, default=4)
    ap.add_argument("--procs", type=int, default=4)
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--scratch", required=True)
    args = ap.parse_args()

    exes = dict(e.split("=", 1) for e in args.exe)
    results = {label: {w: [] for w in workloads()} for label in exes}
    for r in range(args.rounds):
        for label, exe in exes.items():
            def go():
                preload(args.port)
                for w in workloads():
                    results[label][w].append(
                        run_workload(args.port, w, args.seconds, args.procs))
            with_server(exe, args.port, args.scratch, label, go)
            print(f"round {r + 1} {label} done", flush=True)

    labels = list(exes)
    print()
    print(f"{'workload':<16}" + "".join(f"{l:>14}" for l in labels) + f"{'change':>10}")
    for w in workloads():
        meds = [statistics.median(results[l][w]) for l in labels]
        change = (meds[-1] / meds[0] - 1) * 100 if len(meds) > 1 else 0
        print(f"{w:<16}" + "".join(f"{m:>12,.0f}/s" for m in meds) + f"{change:>+9.1f}%")
    print("\nall samples (ops/s):")
    for l in labels:
        for w in workloads():
            print(f"  {l:<5} {w:<16}", [round(x) for x in results[l][w]])


if __name__ == "__main__":
    main()
