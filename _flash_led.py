# -*- coding: utf-8 -*-
"""
501 毕设 —— 最简烧录（只烧 17_led_test）

★ 这个脚本就用最早成功的 flash3 逻辑，没有多余的花样：
    1. 扫描波特率（反复按复位键，找到能握手的）
    2. 用那个波特率烧录
    3. 失败则用更低的波特率再试

用法：双击 flash_led.bat
"""
import os
import sys
import time
import subprocess

import serial
import serial.tools.list_ports

BIN = r"T:\AS\bishe501\17_led_test\Objects\step17_led.bin"
# ★★★ 2026-10-06：19200 优先（用户实测这是能成功的最快波特率）；已取消 2400
BAUDS = [38400, 19200, 9600, 115200, 57600, 4800]


def find_ch340():
    for p in serial.tools.list_ports.comports():
        d = ((p.description or "") + (p.manufacturer or "")).upper()
        if any(k in d for k in ("CH340", "CH341", "USB-SERIAL", "USB SERIAL")):
            return p.device
    return None


def try_sync(port, baud):
    try:
        s = serial.Serial(port, baud, timeout=0.8)
        time.sleep(0.1)
        s.reset_input_buffer()
        s.write(b"\x7f")
        time.sleep(0.3)
        r = s.read(32)
        s.close()
        return bool(r) and r[0] == 0x79
    except Exception:
        return False


print()
print("=" * 60)
print("   501 毕设 —— 最简烧录（17_led_test）")
print("=" * 60)
print()

port = find_ch340()
if not port:
    print("X 没找到 CH340（USB-TTL）")
    input("回车退出...")
    sys.exit(1)

print("  串口: %s" % port)
print("  固件: %s" % BIN)

if not os.path.exists(BIN):
    print("X 找不到固件，先在 Keil 里编译一次，或者跑 flash_any.bat 选 17")
    input("回车退出...")
    sys.exit(1)

sz = os.path.getsize(BIN) / 1024
print("  大小: %.1f KB" % sz)
print()
print("  ★ 请做好这两件事：")
print("       1. BOOT0 跳线帽 -> 1")
print("       2. 按一下复位键（RST）")
print()
input("  做好了按回车，开始...")
print()

# ==================== 阶段 1：扫描波特率 ====================
print("  [1/2] 扫描波特率（★ 请反复按 STM32 的复位键）")
print()

good = None
for attempt in range(1, 25):
    for b in BAUDS:
        sys.stdout.write("    第%2d轮 %-7d ... " % (attempt, b))
        sys.stdout.flush()
        if try_sync(port, b):
            print("★★★ 连上了！")
            good = b
            break
        print("无")
    if good:
        break
    if (attempt % 3) == 0:
        sys.stdout.write("        -> ★ 请按一下复位键（RST）\r\n")
    time.sleep(0.3)

if not good:
    print()
    print("X 扫描失败：24 轮都没连上")
    print()
    print("  检查：")
    print("    1. BOOT0 是不是在【1】")
    print("    2. 按过复位键吗")
    print("    3. USB-TTL 三根线（TXD->A10、RXD->A9、GND->G）")
    print("       试试把 TXD/RXD 对调")
    print("    4. 模块的线全拔掉")
    input("\n按回车退出...")
    sys.exit(1)

print()
print("  ★ 握手成功！可用波特率 = %d" % good)
print()

# ==================== 阶段 2：烧录 ====================
print("  [2/2] 烧录中（★ 别按复位、别碰线）...")
print()

ok = False
used = None

for b in [good] + [x for x in BAUDS if x < good]:
    print("  ---- 试波特率 %d ----" % b)
    try:
        r = subprocess.run(
            [sys.executable, "-m", "stm32loader", "-p", port, "-b", str(b),
             "-e", "-w", "-v", BIN, "-g", "0x08000000"],
            capture_output=True, text=True, errors="replace", timeout=180)
        out = ((r.stdout or "") + (r.stderr or "")).strip()
        rc = r.returncode
    except subprocess.TimeoutExpired:
        print("    超时")
        continue

    for ln in out.splitlines():
        t = ln.strip()
        if t and "progress" not in t.lower():
            print("    " + t)

    if rc == 0:
        ok = True
        used = b
        break
    print("    X %d 失败，换下一个" % b)
    print()

print()
print("=" * 60)
if ok:
    print("  ★★★ 烧录成功！（波特率 %d）" % used)
    print("=" * 60)
    print()
    print("  ★ 现在做这两步：")
    print("       1. BOOT0 跳线帽拨回【0】")
    print("       2. 按一下复位键")
    print()
    print("  ★★ 然后看 STM32 板上的 LED（PC13）")
    print("       应该：常亮2秒 → 灭1秒 → 快闪3次 → 循环")
    print()
    print("  ★ 再双击 serial.bat 看串口打印")
else:
    print("  X 烧录失败")
    print("=" * 60)
    print()
    print("  排查：")
    print("    1. BOOT0 是否真的在【1】")
    print("    2. USB-TTL 三根线，试试 TXD/RXD 对调")
    print("    3. 杜邦线换成短的")
    print("    4. 模块的线全部拔掉")
print()
input("按回车退出...")
