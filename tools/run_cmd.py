#!/usr/bin/env python3
# run_cmd.py — QEMU를 띄워 셸 프롬프트를 감지한 뒤 명령을 보내고 출력을 캡처한다.
# 사용: python3 tools/run_cmd.py "<셸에 보낼 명령>" [대기상한초]
import subprocess, sys, time, select, os

cmd = sys.argv[1] if len(sys.argv) > 1 else "cowtest"
deadline = float(sys.argv[3]) if len(sys.argv) > 3 else 25.0

qemu = [
    "qemu-system-riscv64", "-machine", "virt", "-smp", "3",
    "-bios", "default", "-nographic", "-kernel", "build/kernel.elf",
    "-global", "virtio-mmio.force-legacy=false",
    "-drive", "file=build/fs.img,if=none,format=raw,id=x0",
    "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0",
    "-netdev", "user,id=net0",
    "-device", "virtio-net-device,netdev=net0,bus=virtio-mmio-bus.1",
]
p = subprocess.Popen(qemu, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, bufsize=0)

buf = b""
sent = False
start = time.time()
while time.time() - start < deadline:
    r, _, _ = select.select([p.stdout], [], [], 0.2)
    if r:
        chunk = os.read(p.stdout.fileno(), 4096)
        if not chunk:
            break
        buf += chunk
    # 셸 프롬프트가 보이고, 부팅 출력이 한동안 잠잠하면 명령 전송
    if not sent and b"userspace shell" in buf:
        # 마지막 청크 이후 잠깐 조용해질 때까지 한 번 더 읽기
        time.sleep(1.0)
        try:
            while True:
                r2, _, _ = select.select([p.stdout], [], [], 0.3)
                if not r2:
                    break
                buf += os.read(p.stdout.fileno(), 4096)
        except Exception:
            pass
        p.stdin.write((cmd + "\n").encode())
        p.stdin.flush()
        sent = True
    if sent and (b"\xea\xb2\xa9\xeb\xa6\xac" in buf or b"command not found" in buf
                 or b"EXCEPTION" in buf):
        # "격리"(UTF-8) 또는 종료 신호를 보면 마무리
        time.sleep(0.5)
        r3, _, _ = select.select([p.stdout], [], [], 0.5)
        if r3:
            buf += os.read(p.stdout.fileno(), 4096)
        break

p.terminate()
try:
    p.wait(timeout=3)
except Exception:
    p.kill()

sys.stdout.write(buf.decode(errors="replace"))
