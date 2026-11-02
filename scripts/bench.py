import socket
import time

def read_response(sock):
    """Read one complete RESP response."""
    line = b""
    while not line.endswith(b"\r\n"):
        char = sock.recv(1)
        if not char:
            return None
        line += char

    # Simple string, error, or integer: just the line
    if line[0:1] in (b"+", b"-", b":"):
        return line.decode().strip()

    # Bulk string
    if line[0:1] == b"$":
        length = int(line[1:-2])
        if length == -1:
            return None  # nil
        data = sock.recv(length + 2)  # +\r\n
        return data[:-2].decode()

    return line.decode().strip()

def benchmark_set(host, port, count, pipeline=1):
    """Benchmark SET commands."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))

    start = time.time()

    for i in range(0, count, pipeline):
        batch_size = min(pipeline, count - i)

        # Send batch of SET commands
        batch = ""
        for j in range(batch_size):
            key = f"key:{i+j:012d}"
            value = f"value:{i+j}"
            batch += f"*3\r\n$3\r\nSET\r\n${len(key)}\r\n{key}\r\n${len(value)}\r\n{value}\r\n"

        sock.sendall(batch.encode())

        # Read all responses in batch
        for _ in range(batch_size):
            read_response(sock)

        if (i + batch_size) % 10000 == 0:
            print(f"  {i + batch_size} commands...")

    elapsed = time.time() - start
    sock.close()

    print(f"Completed {count} commands in {elapsed:.2f}s ({count/elapsed:.0f} req/s)")

if __name__ == "__main__":
    print(f"Benchmarking SET...")
    benchmark_set("localhost", 6379, count=100000, pipeline=16)
