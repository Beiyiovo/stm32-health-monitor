# -*- coding: utf-8 -*-
"""
501 毕设 —— 长时间串口记录工具（24 小时连续测试用）

功能：
  · 接收串口数据并【同时显示 + 存文件】
  · 每行带时间戳
  · ★ 按小时自动分文件（防止单个文件太大）
  · ★ USB 掉线自动重连（没人盯着也能跑）
  · ★ 每小时打一行统计（收到多少字节、多少条数据、有没有异常）
  · Ctrl+C 安全退出（会写一段总结）

输出位置：T:\\AS\\bishe501\\日志\\
    501log_20261006_2200.txt    ← 按启动时间命名
    501log_20261006_2300.txt    ← 满 1 小时自动新建

用法：双击 serial_log.bat
"""
import os
import sys
import time
import datetime

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("需要 pyserial")
    sys.exit(1)

LOGDIR = r"T:\AS\bishe501\日志"
BAUD = 115200


def find_ch340():
    for p in serial.tools.list_ports.comports():
        d = ((p.description or "") + (p.manufacturer or "")).upper()
        if any(k in d for k in ("CH340", "CH341", "USB-SERIAL", "USB SERIAL")):
            return p.device
    return None


def main():
    os.makedirs(LOGDIR, exist_ok=True)

    port = find_ch340()
    if not port:
        print()
        print("  ✗ 没找到 CH340（USB-TTL）")
        print("     检查：USB-TTL 插好了吗？驱动装了吗？")
        print()
        input("  按回车退出...")
        return

    print()
    print("=" * 62)
    print("      501 毕设 —— 长时间串口记录（24 小时连续测试）")
    print("=" * 62)
    print()
    print("  串口: %s @ %d" % (port, BAUD))
    print("  日志: %s" % LOGDIR)
    print()
    print("  ★ 现在开始记录。可以关掉屏幕，让它自己跑。")
    print("  ★ 按 Ctrl+C 停止并保存。")
    print("  ★ USB 掉线会自动重连。")
    print()
    print("-" * 62)

    # ---------- 打开串口 ----------
    ser = None
    for attempt in range(1, 4):
        try:
            ser = serial.Serial(port, BAUD, timeout=0.3)
            break
        except Exception as e:
            print("  打开失败（第 %d 次）：%s" % (attempt, str(e)[:50]))
            time.sleep(1)
    if not ser:
        print("  ✗ 打不开串口 —— 是不是 serial.bat 还开着？")
        input("  按回车退出...")
        return

    # ---------- 日志文件 ----------
    def new_logfile():
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M")
        path = os.path.join(LOGDIR, "501log_%s.txt" % ts)
        f = open(path, "a", encoding="utf-8")
        f.write("# 501 毕设连续测试日志\n")
        f.write("# 开始时间: %s\n" % datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
        f.write("# 串口: %s @ %d\n" % (port, BAUD))
        f.write("# " + "-" * 56 + "\n")
        f.flush()
        return f, path

    logf, logpath = new_logfile()
    print("  日志文件: %s" % logpath)
    print()

    # ---------- 统计 ----------
    t_start     = time.time()
    t_file      = time.time()
    t_stat      = time.time()
    bytes_total = 0
    lines_total = 0
    data_lines  = 0      # 有效数据行（$HR=...）
    err_lines   = 0      # 错误/告警行
    reconnects  = 0
    buf = ""

    FILE_ROTATE = 3600   # 每小时换文件
    STAT_PERIOD = 3600   # 每小时打统计

    try:
        while True:
            # --- 读数据 ---
            try:
                chunk = ser.read(512)
            except Exception:
                # ★ 串口掉线 → 重连
                reconnects += 1
                print()
                print("  ⚠ 串口断开（第 %d 次），尝试重连..." % reconnects)
                logf.write("# [%s] ★ 串口断开，尝试重连\n" %
                           datetime.datetime.now().strftime("%H:%M:%S"))
                logf.flush()
                try:
                    ser.close()
                except Exception:
                    pass
                time.sleep(3)
                try:
                    ser = serial.Serial(port, BAUD, timeout=0.3)
                    print("  ✓ 已重连")
                    logf.write("# [%s] ✓ 已重连\n" %
                               datetime.datetime.now().strftime("%H:%M:%S"))
                    logf.flush()
                except Exception:
                    continue
                continue

            now = datetime.datetime.now()

            if chunk:
                bytes_total += len(chunk)
                text = chunk.decode("utf-8", errors="replace")
                buf += text

                # 按行处理
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.rstrip("\r")
                    lines_total += 1

                    if not line.strip():
                        continue

                    # 分类统计
                    if "$HR=" in line:
                        data_lines += 1
                    if ("★" in line) or ("告警" in line) or ("X " in line):
                        err_lines += 1

                    # ★ 写入文件（带时间戳）
                    logf.write("[%s] %s\n" % (now.strftime("%H:%M:%S"), line))

                    # 屏幕显示（不加时间戳，保持可读）
                    print(line)
                sys.stdout.flush()

                # 定期 flush 到磁盘（掉电也不丢数据）
                if lines_total % 20 == 0:
                    logf.flush()

            # --- 每小时换文件 ---
            if time.time() - t_file >= FILE_ROTATE:
                logf.write("# " + "-" * 56 + "\n")
                logf.write("# 本文件结束: %s\n" % now.strftime("%Y-%m-%d %H:%M:%S"))
                logf.close()
                logf, logpath = new_logfile()
                t_file = time.time()
                print()
                print("  ★ 已切换到新日志文件: %s" % logpath)
                print()

            # --- 每小时统计 ---
            if time.time() - t_stat >= STAT_PERIOD:
                t_stat = time.time()
                hours = (time.time() - t_start) / 3600.0
                summary = (
                    "\n"
                    "  ┌──────────────────────────────────────────────┐\n"
                    "  │  ★ 连续运行统计                              │\n"
                    "  ├──────────────────────────────────────────────┤\n"
                    "  │  已运行      : %6.2f 小时                     │\n"
                    "  │  收到字节    : %8d                        │\n"
                    "  │  总行数      : %8d                        │\n"
                    "  │  数据行      : %8d  (每 2 秒一条)          │\n"
                    "  │  告警/错误行 : %8d                        │\n"
                    "  │  串口重连    : %8d 次                      │\n"
                    "  └──────────────────────────────────────────────┘\n"
                ) % (hours, bytes_total, lines_total, data_lines, err_lines, reconnects)
                print(summary)
                logf.write("# " + summary.replace("\n", "\n# ") + "\n")
                logf.flush()

    except KeyboardInterrupt:
        pass
    finally:
        try:
            ser.close()
        except Exception:
            pass

        hours = (time.time() - t_start) / 3600.0
        end_summary = (
            "\n"
            "  ══════════════════════════════════════════════\n"
            "  ★ 测试结束总结\n"
            "  ══════════════════════════════════════════════\n"
            "    开始时间  : %s\n"
            "    结束时间  : %s\n"
            "    ★ 连续运行: %.2f 小时\n"
            "    收到字节  : %d\n"
            "    总行数    : %d\n"
            "    数据行    : %d\n"
            "    告警/错误 : %d\n"
            "    串口重连  : %d 次\n"
            "  ══════════════════════════════════════════════\n"
            "  日志目录: %s\n"
        ) % (
            datetime.datetime.fromtimestamp(t_start).strftime("%Y-%m-%d %H:%M:%S"),
            datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            hours, bytes_total, lines_total, data_lines, err_lines, reconnects,
            LOGDIR,
        )
        print(end_summary)
        try:
            logf.write("# " + end_summary.replace("\n", "\n# ") + "\n")
            logf.close()
        except Exception:
            pass

        if hours >= 24.0:
            print("  ★★ 连续运行已超过 24 小时，满足任务书要求！")
        else:
            print("  ⚠ 本次只跑了 %.2f 小时，任务书要求 ≥24 小时。" % hours)
            print("     可以再启动一次接着跑（日志会自动分文件）。")
        print()

    input("  按回车退出...")


if __name__ == "__main__":
    main()
