# -*- coding: utf-8 -*-
"""
501 毕设 —— ESP32-C3 AT 指令测试工具
接线：USB-TTL TXD→GPIO06(右排22)  RXD→GPIO07(右排23)  GND→右排17
      ESP32-C3 用自己 Type-C 供电
"""
import sys
import time

import serial
import serial.tools.list_ports

LINE = "=" * 64

# ★ ESP-AT 常用波特率：115200 是默认，少数版本是 74880
BAUDS = [115200, 74880, 57600, 9600]

CMDS = [
    ("AT",          "基础连通性测试 —— 应该回 OK"),
    ("AT+GMR",      "查固件版本"),
    ("AT+CWMODE?",  "查 WiFi 工作模式（1=Station 2=AP 3=两者）"),
    ("AT+CWMODE=1", "★ 切成 Station 模式 —— 不切的话扫不到别人"),
    ("AT+CIPSTATUS","查网络连接状态"),
    ("AT+CWLAP",    "★ 扫周围 WiFi —— 能扫到就说明天线好使"),
]


def find_ch340():
    for p in serial.tools.list_ports.comports():
        d = ((p.description or "") + (p.manufacturer or "")).upper()
        if any(k in d for k in ("CH340", "CH341", "USB-SERIAL", "USB SERIAL")):
            return p.device
    return None


def send_at(ser, cmd, wait=2.5):
    """发一条 AT 指令，返回响应文本"""
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    time.sleep(wait)
    raw = ser.read(4096)
    try:
        return raw.decode("utf-8", errors="replace")
    except Exception:
        return repr(raw)


print()
print(LINE)
print("      501 毕设 —— ESP32-C3  AT 指令测试")
print(LINE)
print()

port = find_ch340()
if not port:
    print("✗ 没找到 CH340（USB-TTL）")
    print("  → 把 USB-TTL 从 STM32 上拔下来，接到 ESP32-C3 上")
    input("\n按回车退出...")
    sys.exit(1)
print("USB-TTL: %s" % port)
print()

print(LINE)
print("  请确认做好：")
print("    ① USB-TTL TXD → ESP32-C3 右排 22 孔（GPIO06）")
print("    ② USB-TTL RXD → ESP32-C3 右排 23 孔（GPIO07）")
print("    ③ USB-TTL GND → ESP32-C3 右排 17 孔（GND）")
print("    ④ USB-TTL VCC → 不接")
print("    ⑤ ★ ESP32-C3 用自己 Type-C 线供电（插电脑或充电宝）")
print(LINE)
input("\n  做好了按回车开始测试...")
print()

# ---------- 找波特率 ----------
print(LINE)
print("  [1/2] 找通信波特率")
print(LINE)
print()

ser = None
good_baud = None
for b in BAUDS:
    sys.stdout.write("    试 %-7d ... " % b)
    sys.stdout.flush()
    try:
        s = serial.Serial(port, b, timeout=1.0)
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write(b"AT\r\n")
        time.sleep(1.5)
        resp = s.read(1024).decode("utf-8", errors="replace")
        if "OK" in resp:
            print("★ 成功！")
            ser = s
            good_baud = b
            break
        else:
            print("无响应（收到 %d 字节）" % len(resp))
            s.close()
    except Exception as e:
        print("出错: %s" % e)

if not ser:
    print()
    print(LINE)
    print("  ✗ 所有波特率都没响应。检查：")
    print(LINE)
    print()
    print("  ① ★ 接线对不对（最容易错）：")
    print("        USB-TTL TXD → ESP32-C3 GPIO06（右排第 22 孔）")
    print("        USB-TTL RXD → ESP32-C3 GPIO07（右排第 23 孔）")
    print("        USB-TTL GND → ESP32-C3 GND（右排第 17 孔）")
    print("        ★ TXD/RXD 交叉！试试对调")
    print()
    print("  ② ★ 孔号数对了吗：")
    print("        从【天线端】数起（不是 USB 端）")
    print("        右排第 1 个 = 17，第 6 个 = 22，第 7 个 = 23")
    print()
    print("  ③ ESP32-C3 供电了吗：")
    print("        Type-C 线插上，板上的电源灯应该亮")
    print()
    print("  ④ AT 固件还在吗：")
    print("        ESP32-C3 上电后会从 Type-C 口打印启动日志")
    print("        用另一个串口工具看那个口，能看到启动信息说明固件在")
    print()
    print("  ⑤ 波特率：ESP-AT 默认 115200，少数版本 74880")
    input("\n按回车退出...")
    sys.exit(1)

print()
print("  ★ 通信正常，波特率: %d" % good_baud)
print()

# ---------- 逐条测试 AT 指令 ----------
print(LINE)
print("  [2/2] 逐条测试 AT 指令")
print(LINE)

results = {}
for cmd, desc in CMDS:
    print()
    print("  ── 发送: %s" % cmd)
    print("     （%s）" % desc)
    resp = send_at(ser, cmd, wait=3.0 if cmd == "AT+CWLAP" else 2.0)
    resp_clean = resp.strip()
    if resp_clean:
        for ln in resp_clean.splitlines():
            print("     ← %s" % ln.rstrip())
    else:
        print("     ← （无响应）")
    results[cmd] = resp_clean

ser.close()

# ---------- 结论 ----------
print()
print(LINE)
print("                       测 试 结 论")
print(LINE)
print()

ok_basic = "OK" in results.get("AT", "")
print("  [%s] AT 基础指令        %s" % ("✓" if ok_basic else "✗",
      "OK" if ok_basic else "无响应"))

gmr = results.get("AT+GMR", "")
print("  [%s] 固件版本          %s" % ("✓" if "AT version" in gmr or "OK" in gmr else "✗",
      gmr.splitlines()[0] if gmr else "无"))

wl = results.get("AT+CWLAP", "")
n_ap = wl.count("+CWLAP:")
print("  [%s] 扫 WiFi（天线）   扫到 %d 个热点" % ("✓" if n_ap > 0 else "✗", n_ap))

print()
if ok_basic and n_ap > 0:
    print("  ✓✓✓ 全部通过！ESP32-C3 的 AT 固件工作正常，天线好使。")
    print()
    print("  ★ 下一步：把 USB-TTL 接回 STM32，")
    print("     然后写「STM32 通过 AT 指令控制 ESP32-C3」的代码")
elif ok_basic:
    print("  ✓ AT 通了，但没扫到 WiFi。可能：")
    print("     · 周围确实没热点（换个地方试）")
    print("     · 天线被挡住了")
    print("     · 需要先 AT+CWMODE=1 设置成 Station 模式再扫")
else:
    print("  ✗ AT 没通，检查接线和供电")
print()
input("按回车退出...")
