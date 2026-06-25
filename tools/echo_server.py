#!/usr/bin/env python3
# echo_server.py — TCP 에코 서버(테스트용). 받은 데이터를 그대로 돌려보낸다.
import socket, sys
port = int(sys.argv[1]) if len(sys.argv) > 1 else 5599
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", port))
s.listen(4)
print(f"echo server on 0.0.0.0:{port}", flush=True)
while True:
    try:
        c, addr = s.accept()
        data = c.recv(2048)
        if data:
            c.sendall(data)
        c.close()
    except Exception as e:
        print("err", e, flush=True)
