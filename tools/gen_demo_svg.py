#!/usr/bin/env python3
# gen_demo_svg.py — kernel-hobby 유저공간 셸 데모를 애니메이션 SVG로 생성.
# 외부 의존성 없음. SMIL로 줄별 reveal + 명령어 타이핑 wipe + 깜빡이는 커서.
# 출력은 정적 이미지처럼 어디서나(GitHub/블로그) 임베드된다.

import html

# 팔레트(cover.svg와 동일)
DIM, BRIGHT, PROMPT = "#8a94a6", "#c9d1d9", "#dea584"
GREEN, GREENC, TITLE = "#3a944a", "#27c93f", "#5b6473"

FS = 14.5          # 글꼴 크기
CW = 8.62          # monospace 글자 폭(추정)
X = 66             # 본문 좌측 x
Y0 = 124           # 첫 줄 y
DY = 19.0          # 줄 간격

esc = html.escape

# 줄 정의: (kind, ...)
#  ("out", [(text,color),...], t)          즉시 출력 줄
#  ("cmd", "cmd", t)                        프롬프트 + 타이핑되는 명령
#  ("blank",)                               빈 줄
TYPE_PER_CHAR = 0.07

lines = [
    ("out", [("OpenSBI v1.5.1   ->  S-mode", TITLE)], 0.15),
    ("out", [("kernel-hobby v0.1  (C / RISC-V)", BRIGHT)], 0.35),
    ("ok",  "paging enabled (Sv39)", 0.60),
    ("ok",  "virtio-blk disk ready", 0.85),
    ("ok",  "filesystem mounted: 3 files", 1.10),
    ("blank",),
    ("out", [("kernel-hobby userspace shell. try: ls, cat motd.txt, hello", TITLE)], 1.45),
    ("cmd", "ls", 1.95),
    ("out", [("  motd.txt   readme.txt   hello", DIM)], 2.45),
    ("cmd", "hello", 3.05),
    ("out", [("  [hello] I am a separate program, exec'd from disk!", BRIGHT)], 3.70),
    ("cmd", "cat motd.txt", 4.35),
    ("out", [("Welcome to kernel-hobby (C / RISC-V).", DIM)], 5.30),
    ("out", [("This text lives on a virtio-blk disk, read by the kernel.", DIM)], 5.45),
    ("cmd", "mem", 6.05),
    ("out", [("free pages: 32091  (~125 MB)", DIM)], 6.55),
    ("cmd_final", "", 7.05),  # 마지막 프롬프트 + 깜빡 커서
]

parts = []
clip_defs = []
cur_y = Y0
last_prompt_x = X
last_prompt_y = Y0

def set_visible(begin):
    return f'<set attributeName="visibility" to="visible" begin="{begin:.2f}s" fill="freeze"/>'

for ln in lines:
    kind = ln[0]
    y = cur_y
    if kind == "blank":
        cur_y += DY
        continue
    if kind == "out":
        segs, t = ln[1], ln[2]
        tspans = "".join(f'<tspan fill="{c}">{esc(txt)}</tspan>' for txt, c in segs)
        parts.append(
            f'<text x="{X}" y="{y:.1f}" visibility="hidden">{tspans}'
            f'{set_visible(t)}</text>')
    elif kind == "ok":
        text, t = ln[1], ln[2]
        parts.append(
            f'<text x="{X}" y="{y:.1f}" visibility="hidden">'
            f'<tspan fill="{GREEN}">[ok]</tspan>'
            f'<tspan fill="{DIM}" dx="6">{esc(text)}</tspan>'
            f'{set_visible(t)}</text>')
    elif kind in ("cmd", "cmd_final"):
        cmd, t = ln[1], ln[2]
        # 프롬프트 "$ "
        parts.append(
            f'<text x="{X}" y="{y:.1f}" fill="{PROMPT}" visibility="hidden">$'
            f'{set_visible(t)}</text>')
        cmd_x = X + 2 * CW
        if kind == "cmd" and cmd:
            # 타이핑 wipe: clip rect width 0 -> full
            cid = f'clip{int(t*100)}'
            w = len(cmd) * CW + 4
            dur = len(cmd) * TYPE_PER_CHAR
            clip_defs.append(
                f'<clipPath id="{cid}"><rect x="{cmd_x:.1f}" y="{y-FS:.1f}" '
                f'width="0" height="{FS+6:.1f}">'
                f'<animate attributeName="width" from="0" to="{w:.1f}" '
                f'begin="{t:.2f}s" dur="{dur:.2f}s" fill="freeze"/></rect></clipPath>')
            parts.append(
                f'<text x="{cmd_x:.1f}" y="{y:.1f}" fill="{BRIGHT}" '
                f'clip-path="url(#{cid})">{esc(cmd)}</text>')
            last_prompt_x = cmd_x + len(cmd) * CW + 3
        else:
            last_prompt_x = cmd_x
        last_prompt_y = y
    cur_y += DY

# 깜빡이는 커서(마지막 프롬프트 뒤)
cursor = (
    f'<rect x="{last_prompt_x:.1f}" y="{last_prompt_y-FS+1:.1f}" width="9" height="{FS+2:.1f}" '
    f'fill="{GREENC}" visibility="hidden">'
    f'<set attributeName="visibility" to="visible" begin="7.15s" fill="freeze"/>'
    f'<animate attributeName="opacity" values="1;1;0;0" dur="1s" begin="7.15s" '
    f'repeatCount="indefinite"/></rect>')

W, H = 740, int(cur_y + 40)
win_h = cur_y - 22

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">
  <defs>
    <style>.mono{{font-family:'SF Mono','JetBrains Mono',Menlo,Consolas,monospace;}}</style>
{chr(10).join("    " + c for c in clip_defs)}
  </defs>
  <rect width="{W}" height="{H}" fill="#0b0e14"/>
  <circle cx="620" cy="80" r="180" fill="#2a7de1" opacity="0.10"/>
  <circle cx="120" cy="{H-40}" r="150" fill="#3a944a" opacity="0.07"/>
  <rect x="40" y="44" width="660" height="{win_h:.0f}" rx="14" fill="#11151d" stroke="#2a3140" stroke-width="1.5"/>
  <circle cx="66" cy="68" r="6" fill="#ff5f56"/>
  <circle cx="86" cy="68" r="6" fill="#ffbd2e"/>
  <circle cx="106" cy="68" r="6" fill="#27c93f"/>
  <text x="370" y="72" text-anchor="middle" class="mono" font-size="12" fill="{TITLE}">kernel-hobby — qemu riscv64</text>
  <line x1="40" y1="90" x2="700" y2="90" stroke="#2a3140" stroke-width="1.5"/>
  <g class="mono" font-size="{FS}" xml:space="preserve">
{chr(10).join("    " + p for p in parts)}
    {cursor}
  </g>
</svg>
'''

import sys
out = sys.argv[1] if len(sys.argv) > 1 else "demo.svg"
with open(out, "w") as f:
    f.write(svg)
print(f"wrote {out} ({W}x{H}, {len(lines)} lines)")
