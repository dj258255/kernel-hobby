#!/usr/bin/env python3
# run_tcp_boot.py — 부팅 시 TCP 서버 데모를 검증(셸 입력 불필요, 결정적).
#   QEMU를 hostfwd로 띄우고, 게스트가 "listening"을 찍으면 호스트에서 접속해
#   데이터를 보내고 에코를 받는다.
import subprocess, time, select, os, socket

qemu = ["qemu-system-riscv64","-machine","virt","-smp","3","-bios","default",
        "-nographic","-kernel","build/kernel.elf",
        "-global","virtio-mmio.force-legacy=false",
        "-drive","file=build/fs.img,if=none,format=raw,id=x0",
        "-device","virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0",
        "-netdev","user,id=net0,hostfwd=tcp:127.0.0.1:5599-10.0.2.15:5599",
        "-device","virtio-net-device,netdev=net0,bus=virtio-mmio-bus.1"]
p = subprocess.Popen(qemu, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, bufsize=0)
buf = b""
def drain(t):
    global buf
    end = time.time()+t
    while time.time() < end:
        r,_,_ = select.select([p.stdout],[],[],0.2)
        if r:
            c = os.read(p.stdout.fileno(), 4096)
            if not c: return
            buf += c

start = time.time()
while b"listening on :5599" not in buf and time.time()-start < 30:
    drain(0.3)

host_result = "(no result)"
if b"listening on :5599" in buf:
    try:
        time.sleep(0.3)
        c = socket.create_connection(("127.0.0.1", 5599), timeout=8)
        c.sendall(b"hi from host over TCP\n")
        echo = c.recv(1024)
        host_result = "host got echo: %r" % echo
        c.close()
    except Exception as e:
        host_result = "host failed: %r" % e
else:
    host_result = "guest never reached listening"

drain(5.0)
p.terminate()
try: p.wait(timeout=3)
except Exception: p.kill()
out = buf.decode(errors="replace")
i = out.find("RX probe")
if i < 0: i = out.find("[tcp]")
print(out[i:] if i >= 0 else out[-1500:])
print("=== HOST SIDE ===")
print(host_result)
