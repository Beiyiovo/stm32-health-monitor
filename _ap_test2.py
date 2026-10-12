# -*- coding: utf-8 -*-
"""
501 毕设 —— ESP32-C3 配置 + TCP 连通性测试（v2，过滤乱码）
1) 把 ESP32-C3 配成 AP 模式 + TCP 服务器
2) 然后守着串口，等你手机连上来 —— 连上会打印 0,CONNECT
"""
import re
import sys
import time

import serial
import serial.tools.list_ports

LINE = "=" * 64
BAUDS = [115200, 74880, 57600, 9600]

AP_SSID = "HealthHelper"
AP_PWD = "12345678"
AP_CHANNEL = 5
AP_ECN = 3
TCP_PORT = 8080


def find_ch340():
    for p in serial.tools.list_ports.comports():
        d = ((p.description or "") + (p.manufacturer or "")).upper()
        if any(k in d for k in ("CH340", "CH341", "USB-SERIAL", "USB SERIAL")):
            return p.device
    return None


def readable(raw):
    """把乱码过滤掉，只留可读的 ASCII 行"""
    txt = raw.decode("utf-8", errors="replace")
    out = []
    for ln in txt.replace("\r", "\n").split("\n"):
        ln = ln.strip()
        if not ln:
            continue
        # 只保留可打印 ASCII 占比高的行
        printable = sum(1 for c in ln if 32 <= ord(c) < 127 or c in "\t")
        if len(ln) and printable / len(ln) > 0.85:
            out.append(ln)
    return out


def at(ser, cmd, wait=1.5):
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    time.sleep(wait)
    raw = ser.read(8192)
    lines = readable(raw)
    print("    → %s" % cmd)
    if lines:
        for ln in lines:
            print("       %s" % ln)
    else:
        print("       （无有效响应）")
    return lines


print()
print(LINE)
print("   501 毕设 —— ESP32-C3 配置 + TCP 连通性测试")
print(LINE)
print()
print("  接线：USB-TTL TXD → 右排 22 孔（GPIO06）")
print("        USB-TTL RXD → 右排 23 孔（GPIO07）")
print("        USB-TTL GND → 右排 17 孔（GND）")
print("        ESP32-C3 用自己 Type-C 供电")
print()
input("  做好了按回车...")
print()

port = find_ch340()
if not port:
    print("✗ 没找到 CH340")
    input("\n按回车退出...")
    sys.exit(1)

ser = None
for b in BAUDS:
    sys.stdout.write("  试波特率 %-7d ... " % b)
    sys.stdout.flush()
    try:
        s = serial.Serial(port, b, timeout=0.5)
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write(b"AT\r\n")
        time.sleep(1.2)
        if "OK" in s.read(1024).decode("utf-8", errors="replace"):
            print("★ 成功")
            ser = s
            break
        print("无")
        s.close()
    except Exception as e:
        print("错 %s" % e)

if not ser:
    print("\n✗ 连不上 ESP32-C3")
    input("\n按回车退出...")
    sys.exit(1)

print()
print(LINE)
print("  [1/2] 配置成 AP 模式 + TCP 服务器")
print(LINE)
print()

# ★ 不用 AT+RST（重启会吐一堆 74880 的乱码日志，没必要）
at(ser, "AT", 1.2)
at(ser, "AT+CWMODE=2", 1.5)
at(ser, 'AT+CWSAP="%s","%s",%d,%d' % (AP_SSID, AP_PWD, AP_CHANNEL, AP_ECN), 2.5)
at(ser, "AT+CIPMUX=1", 1.5)
at(ser, "AT+CIPSERVER=1,%d" % TCP_PORT, 1.5)
ip_lines = at(ser, "AT+CIFSR", 2.0)
at(ser, "AT+CIPSTATUS", 1.5)

print()
print(LINE)
print("  [2/2] ★ 守着串口，等手机连上来")
print(LINE)
print()
print("  现在在手机上做：")
print("    ① 连热点：%s   密码：%s" % (AP_SSID, AP_PWD))
print("    ② 装「网络调试助手」，选 TCP Client")
print("    ③ 地址 192.168.4.1   端口 %d" % TCP_PORT)
print("    ④ 点【连接】")
print()
print("  ★ 手机连上的瞬间，这里会打印 0,CONNECT")
print("  ★ 按 Ctrl+C 结束")
print()
print("-" * 64)

ser.timeout = 0.3
try:
    seen = False
    t0 = time.time()
    while True:
        raw = ser.read(2048)
        if raw:
            for ln in readable(raw):
                mark = ""
                if "CONNECT" in ln.upper():
                    mark = "   ★★★ 手机连上了！"
                    seen = True
                elif "CLOSED" in ln.upper():
                    mark = "   （手机断开了）"
                elif "+IPD" in ln:
                    mark = "   ← 收到手机发来的数据"
                print("  [%6.1fs] %s%s" % (time.time() - t0, ln, mark))
        time.sleep(0.05)
except KeyboardInterrupt:
    pass

print()
print("-" * 64)
if seen:
    print("  ✓✓✓ 手机成功连上 ESP32 的 TCP 服务器！")
    print("      → ESP32-C3 这一环彻底打通")
    print()
    print("  ★ 下一步：写 STM32 的程序，让它采数据后通过串口发给 ESP32")
else:
    print("  ⚠ 还没看到 CONNECT。检查：")
    print("     · 手机连上热点了吗")
    print("     · App 里地址填 192.168.4.1、端口 8080 了吗")
    print("     · App 选的是【TCP Client】不是 TCP Server 吗")
ser.close()
print()
input("按回车退出...")
