#!/usr/bin/env python3
# run_tcp.py — TCP 서버 데모 검증.
#   QEMU를 hostfwd(호스트 127.0.0.1:5599 → 게스트 10.0.2.15:5599)로 띄우고,
#   셸에서 `tcp`를 실행해 게스트를 리스닝시킨 뒤, 호스트에서 접속해 데이터를 보내고
#   에코를 받는다. 게스트/호스트 양쪽 출력을 보여준다.
import subprocess, sys, time, select, os, socket

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

def drain_until_quiet(maxt=10.0, quiet=1.5):
    global buf
    end = time.time()+maxt; last=len(buf); sil=time.time()
    while time.time() < end:
        r,_,_ = select.select([p.stdout],[],[],0.2)
        if r:
            c = os.read(p.stdout.fileno(), 4096)
            if not c: return
            buf += c
        if len(buf)!=last: last=len(buf); sil=time.time()
        elif time.time()-sil > quiet: return

# 부팅 대기 → 조용해질 때까지 → tcp 명령 전송
start = time.time()
while b"userspace shell" not in buf and time.time()-start < 30:
    drain(0.3)
drain_until_quiet()
p.stdin.write(b"tcp\n"); p.stdin.flush()

# 게스트가 listening 찍을 때까지 대기
start = time.time()
while b"listening on :5599" not in buf and time.time()-start < 15:
    drain(0.3)

# 호스트에서 접속 → 데이터 송신 → 에코 수신
host_result = "(host: no result)"
try:
    time.sleep(0.5)
    c = socket.create_connection(("127.0.0.1", 5599), timeout=8)
    c.sendall(b"hi from host over TCP\n")
    echo = c.recv(1024)
    host_result = "host sent 'hi from host over TCP', got echo: %r" % echo
    c.close()
except Exception as e:
    host_result = "host connect/echo failed: %r" % e

drain(4.0)
p.terminate()
try: p.wait(timeout=3)
except Exception: p.kill()

# 게스트 셸 이후 출력만
out = buf.decode(errors="replace")
idx = out.find("userspace shell")
print(out[idx:] if idx >= 0 else out)
print("=== HOST SIDE ===")
print(host_result)
