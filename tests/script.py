import socket

def send_cmd(cmd):
    with socket.create_connection(("localhost", 8080)) as sock:
        sock.sendall(cmd.encode() + b"\r\n")
        return sock.recv(4096).decode()

assert send_cmd("SET mykey 123") == "+OK\r\n"
assert send_cmd("GET mykey") == "$3\r\n123\r\n"
assert send_cmd("DEL mykey") == ":1\r\n"

