# -*- coding: utf-8 -*-
"""
501 毕设 —— 简易串口助手
双击「串口助手.bat」运行；或直接 python _serial_mon.py
自动找串口、自动 115200，把收到的内容打到屏幕上。
"""
import sys
import time
import glob

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("缺少 pyserial，正在安装...")
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pyserial"])
    import serial
    import serial.tools.list_ports


def find_port():
    """优先找 CH340 / USB-SERIAL，其次找唯一的串口"""
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return None, []

    # 优先 CH340 / CH341 / USB-SERIAL
    for p in ports:
        desc = (p.description or "") + (p.manufacturer or "")
        if any(k in desc.upper() for k in ("CH340", "CH341", "USB-SERIAL", "USB SERIAL")):
            return p.device, ports

    # 只有一个非蓝牙串口 → 用它
    non_bt = [p for p in ports if "蓝牙" not in (p.description or "") and "BTHENUM" not in (p.hwid or "")]
    if len(non_bt) == 1:
        return non_bt[0].device, ports

    return None, ports


def main():
    print()
    print("=" * 62)
    print("      501 毕设  ——  串口助手（115200 8 N 1）")
    print("=" * 62)
    print()

    dev, allports = find_port()

    if not allports:
        print("  ✗ 一个串口都没找到！")
        print()
        print("  检查：")
        print("    1. USB 线插好了吗？（有些线只能充电不能传数据）")
        print("    2. 驱动装了吗？（CH340 驱动）")
        print("    3. 板子上电了吗？（红灯亮）")
        print()
        input("  按回车退出...")
        return

    print("  系统里找到这些串口：")
    for i, p in enumerate(allports):
        mark = "  ← 会用这个" if p.device == dev else ""
        print("    [%d] %-6s  %s%s" % (i, p.device, p.description or "", mark))
    print()

    if dev is None:
        print("  ⚠ 没自动认出 CH340，请手动选一个：")
        try:
            sel = input("  输入序号（或直接输入 COM 号）：").strip()
            if sel.isdigit() and int(sel) < len(allports):
                dev = allports[int(sel)].device
            else:
                dev = sel.upper()
                if not dev.startswith("COM"):
                    dev = "COM" + dev
        except Exception:
            return

    print()
    print("  打开 %s @ 115200 ..." % dev)
    try:
        ser = serial.Serial(dev, 115200, timeout=0.2)
    except Exception as e:
        print("  ✗ 打开失败：%s" % e)
        print()
        print("  可能原因：")
        print("    · 串口被别的软件占用（关掉 FlyMcu / 其他串口助手）")
        print("    · 板子没插好")
        print()
        input("  按回车退出...")
        return

    print("  ✓ 已打开。正在接收数据（按 Ctrl+C 退出）...")
    print()
    print("  ★ 如果 10 秒内一个字节都没收到，我会提示可能的原因")
    print("-" * 62)
    print()

    buf = b""
    total_bytes = 0
    warned = False
    t_start = time.time()
    try:
        while True:
            data = ser.read(256)
            if data:
                total_bytes += len(data)
                buf += data
                # 按行输出，处理 \r\n
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    try:
                        text = line.decode("utf-8", errors="replace").rstrip("\r")
                    except Exception:
                        text = repr(line)
                    print(text)
                sys.stdout.flush()
            else:
                # ★ 10 秒没收到任何数据 → 提示排查
                if (not warned) and total_bytes == 0 and (time.time() - t_start) > 10:
                    warned = True
                    print()
                    print("  " + "!" * 58)
                    print("  ★ 已经 10 秒没收到任何数据了。逐个检查：")
                    print()
                    print("     1. ★ BOOT0 跳线帽拨回【0】了吗？")
                    print("     2. ★ 按过复位键（RST）吗？")
                    print("     3. STM32 板上的板载 LED 在闪吗？")
                    print("          · 在闪  → 程序在跑，问题在串口这边")
                    print("          · 不闪  → 程序没跑（多半是 BOOT0 / 复位）")
                    print("     4. USB-TTL 的 RXD 接到 STM32 印着【A9】的针了吗？")
                    print("        （看打印只需要 RXD 和 GND 两根线）")
                    print("     5. 烧录脚本的窗口关掉了吗？（串口可能还被它占着）")
                    print()
                    print("  " + "!" * 58)
                    print()
                    sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        print()
        print("-" * 62)
        print("  串口已关闭。共收到 %d 字节。" % total_bytes)


if __name__ == "__main__":
    main()
