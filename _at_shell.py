# -*- coding: utf-8 -*-
"""
501 毕设 —— ESP32-C3 交互式 AT 命令行
用法：双击 at_shell.bat
   · ESP32 的输出会实时显示
   · 你输入 AT 指令（不用写 AT，直接打指令也行），回车发送
   · 输入 quit 退出

接线：USB-TTL TXD→GPIO06(右排22)  RXD→GPIO07(右排23)  GND→右排17(天线端数第1个)
      ESP32-C3 用自己 Type-C 供电
"""
import sys
import time
import threading

import serial
import serial.tools.list_ports

LINE = "=" * 64
BAUDS = [115200, 74880, 57600, 9600]

# 常用指令速查
HELP = [
    ("AT",                       "基础测试，回 OK"),
    ("AT+GMR",                   "查固件版本"),
    ("AT+CWMODE?",               "查 WiFi 模式（1=Station 2=AP 3=两者）"),
    ("AT+CWMODE=1",              "★ 切成 Station 模式（才能扫别人）"),
    ("AT+CWMODE=3",              "Station + AP 共存"),
    ("AT+CWLAP",                 "★ 扫周围 WiFi（需 Station 模式）"),
    ("AT+CWJAP=\"ssid\",\"pwd\"", "连路由器（换成你的 WiFi 名和密码）"),
    ("AT+CIFSR",                 "查自己拿到的 IP"),
    ("AT+CIPSTATUS",             "查连接状态"),
    ("AT+RST",                   "重启模块"),
    ("AT+MQTTUSERCFG=?",         "查 MQTT 配置指令用法（巴法云要用）"),
    ("quit",                     "退出本工具"),
]


def find_ch340():
    for p in serial.tools.list_ports.comports():
        d = ((p.description or "") + (p.manufacturer or "")).upper()
        if any(k in d for k in ("CH340", "CH341", "USB-SERIAL", "USB SERIAL")):
            return p.device
    return None


def reader(ser, stop):
    """后台线程：实时把串口收到的内容打出来"""
    while not stop.is_set():
        try:
            n = ser.in_waiting
            if n:
                data = ser.read(n)
                txt = data.decode("utf-8", errors="replace")
                sys.stdout.write(txt)
                sys.stdout.flush()
            else:
                time.sleep(0.05)
        except Exception:
            break


print()
print(LINE)
print("      501 毕设 —— ESP32-C3  AT 命令行")
print(LINE)
print()
print("  接线确认：")
print("    USB-TTL TXD → ESP32-C3 右排 22 孔（GPIO06）")
print("    USB-TTL RXD → ESP32-C3 右排 23 孔（GPIO07）")
print("    USB-TTL GND → ESP32-C3 右排 17 孔（GND）")
print("    ESP32-C3 用自己 Type-C 供电")
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
        resp = s.read(1024).decode("utf-8", errors="replace")
        if "OK" in resp:
            print("★ 成功")
            ser = s
            break
        print("无")
        s.close()
    except Exception as e:
        print("错 %s" % e)

if not ser:
    print()
    print("✗ 没找到可用波特率。检查接线和供电。")
    input("\n按回车退出...")
    sys.exit(1)

print()
print(LINE)
print("  ✓ 已连上 %s @ %d" % (port, ser.baudrate))
print(LINE)
print()
print("  常用指令速查：")
for c, d in HELP:
    print("    %-30s %s" % (c, d))
print()
print("  ★ 直接输入指令回车发送（不用输 AT 也行，会自动补）")
print("  ★ 输入 quit 退出")
print(LINE)
print()

stop = threading.Event()
th = threading.Thread(target=reader, args=(ser, stop), daemon=True)
th.start()

# 先自动发一条 AT 看看
ser.write(b"AT\r\n")

try:
    while True:
        try:
            cmd = input("AT> ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if not cmd:
            continue
        if cmd.lower() in ("quit", "exit", "q"):
            break

        # 自动补 AT 前缀
        if not cmd.upper().startswith("AT"):
            cmd = "AT+" + cmd

        ser.write((cmd + "\r\n").encode())
finally:
    stop.set()
    time.sleep(0.2)
    ser.close()
    print()
    print("  串口已关闭。")
