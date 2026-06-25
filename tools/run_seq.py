#!/usr/bin/env python3
# run_seq.py — 셸 프롬프트를 감지하며 여러 명령을 순서대로 보낸다(회귀/누수 확인용).
# 사용: python3 tools/run_seq.py "mem" "hello" "hello" "mem"
import subprocess, sys, time, select, os

cmds = sys.argv[1:] or ["mem", "hello", "hello", "mem"]
qemu = ["qemu-system-riscv64","-machine","virt","-smp","3","-bios","default",
        "-nographic","-kernel","build/kernel.elf",
        "-global","virtio-mmio.force-legacy=false",
        "-drive","file=build/fs.img,if=none,format=raw,id=x0",
        "-device","virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0",
        "-netdev","user,id=net0",
        "-device","virtio-net-device,netdev=net0,bus=virtio-mmio-bus.1"]
p = subprocess.Popen(qemu, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, bufsize=0)
buf = b""
def drain(t):
    global buf
    end = time.time() + t
    while time.time() < end:
        r,_,_ = select.select([p.stdout],[],[],0.2)
        if r:
            c = os.read(p.stdout.fileno(), 4096)
            if not c: return False
            buf += c
    return True

def drain_until_quiet(maxt=8.0, quiet=1.2):
    # 출력이 quiet초 동안 멈출 때까지(또는 maxt초까지) 읽는다.
    global buf
    end = time.time() + maxt
    last = len(buf)
    silence = time.time()
    while time.time() < end:
        r,_,_ = select.select([p.stdout],[],[],0.2)
        if r:
            c = os.read(p.stdout.fileno(), 4096)
            if not c: return
            buf += c
        if len(buf) != last:
            last = len(buf); silence = time.time()
        elif time.time() - silence > quiet:
            return

# 부팅 + net 데모가 끝나길 기다린다
start = time.time()
while b"userspace shell" not in buf and time.time()-start < 30:
    drain(0.3)
drain_until_quiet()      # 부팅/네트워크/하트 메시지가 잠잠해질 때까지
for c in cmds:
    p.stdin.write((c+"\n").encode()); p.stdin.flush()
    drain_until_quiet()  # 이 명령 출력이 끝날 때까지 받고 다음 명령
p.terminate()
try: p.wait(timeout=3)
except Exception: p.kill()
sys.stdout.write(buf.decode(errors="replace"))
