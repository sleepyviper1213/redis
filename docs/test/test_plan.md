Validating both **functional correctness** and **performance/stability**.

---

## 1. **Functional Testing** – Does it behave correctly?

###  Manual Testing via Telnet or Netcat

Use a basic TCP client to connect and test your protocol.

```bash
nc localhost 6379
```

Send raw commands:

```
SET foo bar
GET foo
DEL foo
```

Verify that responses are correct:

* `+OK` for success
* `$3\r\nbar\r\n` for GET "bar"
* `-ERR unknown command` for invalid input

---

###  Automated Tests via Script

Write simple test scripts to send requests and assert responses.

**Example using Python:**

```python
import socket

def send_cmd(cmd):
    with socket.create_connection(("localhost", 6379)) as sock:
        sock.sendall(cmd.encode() + b"\r\n")
        return sock.recv(4096).decode()

assert send_cmd("SET mykey 123") == "+OK\r\n"
assert send_cmd("GET mykey") == "$3\r\n123\r\n"
assert send_cmd("DEL mykey") == ":1\r\n"
```

You can also use C++ test clients, e.g. using Boost.Asio or raw sockets.

---

### Unit Tests for Internal Components

Write unit tests for classes like:

* `CommandParser`
* `SnapshotManager`
* `KeyValueStore`

Example with Google Test:

```cpp
TEST(KeyValueStoreTest, BasicSetGet) {
    KeyValueStore store;
    store.set("key", "value");
    EXPECT_EQ(store.get("key"), "value");
}
```

---

## 2. **Integration Testing** – Do components work together?

### Test flows like:

* Client connects → sends SET → gets value
* Trigger SAVE → verify snapshot is written
* Restart server → load snapshot → value is preserved

Include:

* Multiple clients
* Concurrent requests (thread safety)
* Fault simulation (e.g. corrupted snapshot file)

---

##  3. **Performance Testing** – Is it fast enough?

### 🔸 Benchmark with Redis-compatible tools

If your server supports a Redis-like protocol, you can use:

```bash
redis-benchmark -h localhost -p 6379 -n 100000 -c 50 -t set,get
```

Otherwise, write your own benchmark client in C++ or Python:

* Measure latency per request
* Measure throughput (ops/sec)

---

##  4. **Stability & Fault Testing** – Does it handle edge cases?

* Test invalid/malformed commands.
* Disconnect client in the middle of command.
* Snapshot during high load.
* Restart with corrupted or missing snapshot file.
* Use valgrind or AddressSanitizer to detect memory issues.

```bash
valgrind ./your_redis_server
```

---

## 5. **Regression Testing**

After changes, rerun all your tests to make sure nothing breaks.

Automate this with a small script:

```bash
./build/my_redis_server &
sleep 1
pytest test_suite.py
killall my_redis_server
```

Or use CMake + Google Test + CI pipeline for professional setup.

---

##  Optional Tools

* **Wireshark**: Inspect network packets for debugging protocol.
* **tmux + netcat**: Simulate multiple clients manually.
* **ASan + TSan**: Memory + Thread safety.
