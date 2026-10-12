# -*- coding: utf-8 -*-
"""
501 毕设 —— 一键把 ESP32-C3 配成【AP 模式 + TCP 服务器】
之后手机连 ESP32 的热点，用 TCP 客户端连 192.168.4.1:8080 就能收数据。

接线：USB-TTL TXD→GPIO06(右排22)  RXD→GPIO07(右排23)  GND→右排17
      ESP32-C3 用自己 Type-C 供电
"""
import sys
import time

import serial
import serial.tools.list_ports

LINE = "=" * 64
BAUDS = [115200, 74880, 57600, 9600]

# ★ 热点设置（想改就改这两行）
AP_SSID = "HealthHelper"
AP_PWD = "12345678"
AP_CHANNEL = 5
AP_ECN = 3          # 0=开放 2=WPA 3=WPA2 4=WPA/WPA2
TCP_PORT = 8080


def find_ch340():
    for p in serial.tools.list_ports.comports():
        d = ((p.description or "") + (p.manufacturer or "")).upper()
        if any(k in d for k in ("CH340", "CH341", "USB-SERIAL", "USB SERIAL")):
            return p.device
    return None


def at(ser, cmd, wait=1.5, show=True):
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    time.sleep(wait)
    raw = ser.read(4096)
    txt = raw.decode("utf-8", errors="replace").strip()
    if show:
        print("    → %s" % cmd)
        if txt:
            for ln in txt.splitlines():
                print("       %s" % ln.rstrip())
        else:
            print("       （无响应）")
    return txt


print()
print(LINE)
print("      501 毕设 —— 配置 ESP32-C3 为 AP + TCP 服务器")
print(LINE)
print()
print("  接线：USB-TTL TXD → ESP32-C3 右排 22 孔（GPIO06）")
print("        USB-TTL RXD → ESP32-C3 右排 23 孔（GPIO07）")
print("        USB-TTL GND → ESP32-C3 右排 17 孔（GND）")
print("        ESP32-C3 用自己 Type-C 供电")
print()
input("  做好了按回车...")
print()

port = find_ch340()
if not port:
    print("✗ 没找到 CH340（USB-TTL）")
    input("\n按回车退出...")
    sys.exit(1)

# 找波特率
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
    print("\n✗ 连不上 ESP32-C3，检查接线和供电。")
    input("\n按回车退出...")
    sys.exit(1)

print()
print(LINE)
print("  开始配置（每步都会等它回 OK）")
print(LINE)
print()

steps = [
    ("AT", 1.2),
    ("AT+RST", 3.0),                                   # 先重启，清干净状态
    ("AT+CWMODE=2", 1.5),                              # ★ AP 模式
    ('AT+CWSAP="%s","%s",%d,%d' % (AP_SSID, AP_PWD, AP_CHANNEL, AP_ECN), 2.5),  # 建热点
    ("AT+CIPMUX=1", 1.5),                              # 多连接
    ("AT+CIPSERVER=1,%d" % TCP_PORT, 1.5),             # ★ 开 TCP 服务器
    ("AT+CIFSR", 2.0),                                 # 查 IP
    ("AT+CIPSTATUS", 1.5),                             # 查状态
]

for cmd, w in steps:
    at(ser, cmd, wait=w)

# 顺便看看 MQTT 支不支持（以后可能用）
print()
print("  ── 顺带查一下 MQTT 指令支持情况")
mq = at(ser, "AT+MQTTUSERCFG=?", wait=1.5)
mqtt_ok = "MQTTUSERCFG" in mq

ser.close()

print()
print(LINE)
print("                       配 置 结 束")
print(LINE)
print()
print("  ★ 现在这样做：")
print()
print("    1. 手机 → 设置 → WLAN → 连这个热点：")
print("         WiFi 名 ：%s" % AP_SSID)
print("         密码    ：%s" % AP_PWD)
print()
print("    2. 手机上装一个【网络调试助手】App")
print("         （应用商店搜「网络调试助手」或「TCP 调试」）")
print()
print("    3. 在 App 里选【TCP Client】，连：")
print("         地址：192.168.4.1        ← ★ AP 模式的固定 IP")
print("         端口：%d" % TCP_PORT)
print()
print("    4. 点连接 —— 连上后，ESP32 的串口会打印 CONNECT")
print()
print("  MQTT 支持：%s" % ("✓ 支持（备选方案可用）" if mqtt_ok else "✗ 不支持"))
print()
print("  ★ 注意：这个配置【断电就丢】，每次上电要重新配。")
print("     以后我会把这段配置写进 STM32 的程序里，开机自动配。")
print()
input("按回车退出...")
