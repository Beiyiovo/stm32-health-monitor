# -*- coding: utf-8 -*-
"""
501 毕设 —— 烧录排查工具（进阶版）
做 3 件事：
  1. 回环测试（验证 USB-TTL 本身好不好）
  2. 自动扫波特率握手（验证 BOOT0 + 接线）
  3. 给出明确结论和下一步
"""
import sys
import time

import serial
import serial.tools.list_ports

LINE = "=" * 64


def find_ch340():
    for p in serial.tools.list_ports.comports():
        d = ((p.description or "") + (p.manufacturer or "")).upper()
        if any(k in d for k in ("CH340", "CH341", "USB-SERIAL", "USB SERIAL")):
            return p.device
    return None


def ask(prompt):
    try:
        return input(prompt)
    except Exception:
        return ""


print()
print(LINE)
print("        501 毕设 —— 烧录排查工具")
print(LINE)
print()

port = find_ch340()
if not port:
    print("✗ 没找到 CH340（USB-TTL）。插好再来。")
    ask("\n按回车退出...")
    sys.exit(1)
print("找到 USB-TTL: %s\n" % port)

# ============ 测试 1：回环 ============
print(LINE)
print("[测试 1] 回环测试 —— 验证 USB-TTL 本身")
print(LINE)
print()
print("  请这样做：")
print("    ★ 把 USB-TTL 的 TXD 和 RXD 【用一根杜邦线直接连起来】")
print("      （或者用一个跳线帽 / 镊子尖把这两个针短接）")
print("      ★ 不要接 STM32，就 USB-TTL 自己短接")
print()
ask("  短接好了按回车，开始测试...")
print()

loopback_ok = False
try:
    ser = serial.Serial(port, 115200, timeout=0.5)
    time.sleep(0.2)
    ser.reset_input_buffer()
    ser.write(b"HELLO501")
    time.sleep(0.3)
    got = ser.read(64)
    ser.close()
    if b"HELLO501" in got:
        print("  ✓✓✓ 回环成功！收到: %r" % got)
        print("      → USB-TTL 是好的，驱动也没问题")
        loopback_ok = True
    else:
        print("  ✗ 没收到自己发的数据（收到: %r）" % got)
        print("      → 可能：")
        print("        · 短接没做好（TXD/RXD 没真连上）")
        print("        · USB-TTL 坏了")
except Exception as e:
    print("  ✗ 出错: %s" % e)
print()

# ============ 测试 2：扫波特率握手 ============
print(LINE)
print("[测试 2] 和 STM32 bootloader 握手（自动扫波特率）")
print(LINE)
print()
if loopback_ok:
    print("  请这样做：")
    print("    ★ 把 USB-TTL 和 STM32 按下面接好（3 根）")
    print("        TXD  →  STM32 下排第 7 孔（PA10）")
    print("        RXD  →  STM32 下排第 6 孔（PA9）")
    print("        GND  →  STM32 下排第 19 孔（GND）")
    print("        VCC  →  不接")
    print("    ★ BOOT0 跳线帽拨到【靠 1 那边】")
    print("    ★ Micro USB 线给 STM32 供电（红灯亮）")
    print("    ★ 按一下 STM32 的复位键")
    print()
    ask("  都做好了按回车，开始扫描...")
    print()

BAUDS = [115200, 57600, 38400, 19200, 9600]
hit = None
for b in BAUDS:
    try:
        ser = serial.Serial(port, b, timeout=1.0)
        time.sleep(0.15)
        ser.reset_input_buffer()
        ser.write(b"\x7f")          # STM32 bootloader 同步字节
        time.sleep(0.35)
        resp = ser.read(32)
        ser.close()
        if resp and resp[0] == 0x79:
            print("  ✓✓✓ %-7d 收到 ACK (0x79) —— 成功！" % b)
            hit = b
            break
        elif resp:
            print("  ?   %-7d 收到 %s（不是 ACK）" % (b, resp.hex()))
        else:
            print("  ✗   %-7d 无回应" % b)
    except Exception as e:
        print("  ✗   %-7d 打开失败: %s" % (b, e))
print()

# ============ 结论 ============
print(LINE)
print("                       诊 断 结 论")
print(LINE)
print()
if loopback_ok and hit:
    print("  ✓✓ 全部通过！可以烧录了。")
    print("     → 双击 T:\\AS\\bishe501\\flash.bat")
elif not loopback_ok:
    print("  ✗ USB-TTL 回环测试就没过 —— 先解决这个：")
    print("      · 确认 TXD 和 RXD 是真短接了（用万用表通断档量一下）")
    print("      · 换一根杜邦线再试")
    print("      · 还不行 → USB-TTL 可能坏了，换一个")
else:
    print("  ✗ USB-TTL 是好的，但和 STM32 握不上手。按概率排查：")
    print()
    print("  【① 最可能：TXD/RXD 接反了】")
    print("      USB-TTL TXD → STM32 下排 7 孔（PA10）")
    print("      USB-TTL RXD → STM32 下排 6 孔（PA9）")
    print("      ★ 试试【对调这两根线】再跑一遍本工具")
    print()
    print("  【② 孔位数错了】")
    print("      ★ 从【USB 口那一端】数：")
    print("         下排 6 = PA9，7 = PA10，19 = GND")
    print("      ★ 反过来数是 15、14、2")
    print("      用万用表量：STM32 下排 19 孔 对 USB-TTL 的 GND 应该【响】")
    print()
    print("  【③ BOOT0 没拨对】")
    print("      跳线帽要盖住【中间针 + 靠'1'那个针】")
    print("      拨完【必须按复位键】")
    print()
    print("  【④ 复位键没按】")
    print("      ★ 拨完跳线后，按一下板上的 RST 按钮（不是松手才拨）")
    print()
    print("  【⑤ PA9/PA10 被别的东西占了】")
    print("      ★ 如果已经在接面包板，拔掉接在 PA9/PA10 上的其他线")
    print()
    print("  【⑥ GND 没共】")
    print("      必须接 GND！不接的话信号没有参考电平")
print()
ask("按回车退出...")
