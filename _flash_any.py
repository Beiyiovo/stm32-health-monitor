# -*- coding: utf-8 -*-
"""
501 毕设 —— 通用烧录工具 v5（★ 回到最初成功的方案）

★ 说明（2026-10-06）：
   用户反馈 v3/v4 的"直接调 stm32loader 反复重试"一直不成功。
   回到最早成功过的 flash3 逻辑：
     ① 先【扫描】波特率，找到一个能握手的（用 Python 直接发 0x7F 读 ACK）
     ② 再用【那个波特率】调 stm32loader 烧录
     ③ 失败则用更低的波特率再试

   ★ 当时就是用这个逻辑、在 9600 上烧录成功的。

用法：双击 flash_any.bat
"""
import os
import sys
import glob
import time
import subprocess

import serial
import serial.tools.list_ports

LINE = "=" * 64


# ============================================================
#  路径探测（★ 2026-10-12 改）
#  以前这里写死了作者本机的路径，别人 clone 下来直接跑不了。
#  现在按 环境变量 → Keil 注册表 → 常见安装目录 顺序自动找。
# ============================================================
BASE = os.path.dirname(os.path.abspath(__file__))


def _keil_reg_paths():
    """
    读 Keil 注册表里 Products\\MDK 的 Path。
    ★ 坑：这个值通常指向 <Keil根>\\ARM，不是 Keil 根目录本身
      （本机实测：T:\\TE\\ARM）。所以下面既把它当根试，也把它的父目录当根试。
    """
    out = []
    try:
        import winreg
        for hive, key in (
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Keil\Products\MDK"),
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Keil\Products\MDK"),
            (winreg.HKEY_CURRENT_USER,  r"SOFTWARE\Keil\Products\MDK"),
        ):
            try:
                with winreg.OpenKey(hive, key) as k:
                    p, _ = winreg.QueryValueEx(k, "Path")
                    if p:
                        out.append(p.rstrip("\\/"))
            except OSError:
                continue
    except ImportError:
        pass
    return out


def _candidate_roots():
    """按可信度依次给出可能的 Keil 根目录（去重保序）。"""
    roots = []
    for env in ("KEIL_ROOT", "KEIL_MDK_ROOT"):
        v = os.environ.get(env)
        if v and os.path.isdir(v):
            roots.append(v.rstrip("\\/"))
    for p in _keil_reg_paths():
        roots.append(p)                 # 当成是 <root>\ARM
        parent = os.path.dirname(p)
        if parent:
            roots.append(parent)        # 当成是 <root>
    roots += [r"C:\Keil_v5", r"C:\Keil", r"D:\Keil_v5", r"D:\Keil",
              r"E:\Keil_v5", r"T:\TE", r"T:\Keil_v5"]
    seen, out = set(), []
    for r in roots:
        if r and r.lower() not in seen:
            seen.add(r.lower())
            out.append(r)
    return out


def _find_uv4():
    v = os.environ.get("KEIL_UV4")
    if v and os.path.exists(v):
        return v
    for root in _candidate_roots():
        for rel in (("UV4", "UV4.exe"), ("UV4", "uv4.exe")):
            p = os.path.join(root, *rel)
            if os.path.exists(p):
                return p
    return None


def _find_fromelf():
    v = os.environ.get("KEIL_FROMELF")
    if v and os.path.exists(v):
        return v
    for root in _candidate_roots():
        # 注册表给的多半是 <root>\ARM，所以先试 ARMCLANG 直接挂在上面的写法
        for rel in (("ARMCLANG", "bin", "fromelf.exe"),
                    ("ARM", "ARMCLANG", "bin", "fromelf.exe"),
                    ("ARM", "ARMCC", "bin", "fromelf.exe")):
            p = os.path.join(root, *rel)
            if os.path.exists(p):
                return p
    return None


UV4 = _find_uv4()
FROMELF = _find_fromelf()

# ★★★ 2026-10-06 用户实测：19200 是【能成功的最快波特率】
#   烧 27KB 固件：19200 约 15 秒，9600 约 30 秒 → 快一倍
#   ★ 已取消 2400（用户反馈一直烧不进去，且极慢）
BAUDS = [38400, 19200, 9600, 115200, 57600, 4800]

DESC = {
    "01_uart":      "环境验证（串口打印 + LED 心跳）",
    "02_esp_link":  "STM32 <-> ESP32-C3 无线链路",
    "03_i2c_scan":  "★ I2C 扫描（验证传感器接线）",
    "04_oled":      "OLED 显示验证",
    "05_ds18b20":   "温度采集验证",
    "06_mpu6050":   "加速度 + 计步验证",
    "07_max30102":  "心率 + 血氧验证",
    "08_alert":     "蜂鸣器 + LED + 按键验证",
    "09_main":      "★ 完整主程序（最终版）",
    "10_i2c_debug": "I2C 硬件诊断",
    "11_oled_test": "OLED 硬件测试（软件I2C）",
    "12_wire_test": "接线通断测试",
    "13_oled_force":"强制点亮 OLED",
    "14_max_test":  "MAX30102 诊断",
    "15_i2c_hunt":  "I2C 终极排查",
    "16_i2c_scope": "★ 软件示波器式 I2C 诊断",
    "17_led_test":  "★★ 最简单的 LED 测试（只用板载LED）",
    "18_oled_display": "★★★ OLED 综合显示测试（带错误检测）",
    "19_oled_cn":     "★★★ OLED 中文显示（16x16汉字）",
    "20_all_scan":    "★★★ 三模块扫描 + OLED中文（★ 现在烧这个）",
    "21_main_debug":  "★★★ 完整版调试模式（OLED显示全部原始值）",
    "22_max_diag":    "★★★ MAX30102 专项诊断（串口输出）",
    "24_key_test":  "★ 按键硬件测试（判断接线问题）",
    "23_hr_debug":    "★★★ 心率算法内部状态诊断",
}


def find_ch340():
    for p in serial.tools.list_ports.comports():
        d = ((p.description or "") + (p.manufacturer or "")).upper()
        if any(k in d for k in ("CH340", "CH341", "USB-SERIAL", "USB SERIAL")):
            return p.device
    return None


def try_sync(port, baud):
    """（保留旧函数，v6 不再用）"""
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


def try_sync_fast(port, baud, max_tries=80, show=True):
    """
    ★★★ v6.1 快速握手：保持串口打开 + 【批量轰炸】0x7F

    原理：
        STM32 的 bootloader 只要收到一个完整的 0x7F 字节就会回 0x79。
        所以不用"发一个等一个"—— 可以【连发一批，再一次性读】，
        这样避开了每次读超时的等待，速度再快一倍。

    返回 (是否成功, 发了几批)
    """
    BATCH = 8               # 每批发 8 个 0x7F
    BATCH_DELAY = 0.01      # 批内间隔 10ms
    READ_WAIT = 0.04        # 发完一批等 40ms 再读

    try:
        s = serial.Serial(port, baud, timeout=0.05)
        time.sleep(0.06)
        s.reset_input_buffer()

        n_batch = max(1, max_tries // BATCH)

        for b in range(n_batch):
            # 连发一批
            for k in range(BATCH):
                s.write(b"\x7f")
                time.sleep(BATCH_DELAY)

            time.sleep(READ_WAIT)
            r = s.read(64)

            if r and 0x79 in r:
                s.close()
                if show:
                    print("  ★★★ 第 %d 批就成功了！（波特率 %d）" % (b + 1, baud))
                return True, b + 1

            if show and ((b + 1) % 10 == 0):
                sys.stdout.write("      已发 %d 批...\r\n" % (b + 1))
                sys.stdout.flush()

        s.close()
        return False, n_batch
    except Exception as e:
        if show:
            print("      （串口出错：%s）" % str(e)[:50])
        return False, 0


print()
print(LINE)
print("      501 毕设 —— 通用烧录工具 v5")
print("      ★ 回到最初成功的方案（先扫描，再烧录）")
print(LINE)
print()

# ---------- 环境自检（★ 2026-10-12 新增：路径找不到时给清楚提示，而不是莫名报错） ----------
_missing = []
if not os.path.isdir(BASE):
    _missing.append("脚本目录 BASE = %s" % BASE)
if not UV4:
    _missing.append("Keil 命令行编译器 UV4.exe")
if not FROMELF:
    _missing.append("fromelf.exe（用来把 .axf 转成 .bin）")

if _missing:
    print("  X 环境不完整，缺这几样：")
    for m in _missing:
        print("      - %s" % m)
    print()
    print("  怎么解决：")
    print("    1. 装 Keil MDK-ARM（UV4.exe 和 fromelf.exe 都随它一起装）")
    print("    2. 如果 Keil 装在非常规目录，先指定它的根目录再运行本文件：")
    print("         set KEIL_ROOT=D:\\你的Keil目录")
    print("       也可以单独指定：")
    print("         set KEIL_UV4=D:\\Keil_v5\\UV4\\UV4.exe")
    print("         set KEIL_FROMELF=D:\\Keil_v5\\ARM\\ARMCLANG\\bin\\fromelf.exe")
    print()
    input("按回车退出...")
    sys.exit(1)

print("  环境自检 OK")
print("    Keil UV4   : %s" % UV4)
print("    fromelf    : %s" % FROMELF)
print("    工程根目录 : %s" % BASE)
print()

# ---------- 找工程 ----------
projs = []
for f in glob.glob(os.path.join(BASE, "*", "*.uvprojx")):
    projs.append((os.path.basename(os.path.dirname(f)), f))
projs.sort()

if not projs:
    print("X 没找到任何 Keil 工程")
    input("\n按回车退出...")
    sys.exit(1)

print("  找到这些工程：")
print()
for i, (name, path) in enumerate(projs, start=1):
    print("    [%d]  %-14s %s" % (i, name, DESC.get(name, "")))
print()

sel = input("  选哪个？(输入 1~%d，直接回车 = 1): " % len(projs)).strip()
idx = int(sel) - 1 if (sel.isdigit() and 1 <= int(sel) <= len(projs)) else 0

name, projfile = projs[idx]
projdir = os.path.dirname(projfile)
outname = os.path.splitext(os.path.basename(projfile))[0]

# ---------- 编译 ----------
print()
print(LINE)
print("  [1/4] 编译: %s   （%s）" % (name, DESC.get(name, "")))
print(LINE)
subprocess.run([UV4, "-b", os.path.basename(projfile), "-j0", "-o", "bl.txt"],
               cwd=projdir, capture_output=True, text=True, errors="replace")
logf = os.path.join(projdir, "bl.txt")
if os.path.exists(logf):
    txt = open(logf, encoding="utf-8", errors="replace").read()
    os.remove(logf)
    for ln in txt.splitlines():
        if "Program Size" in ln or "Error(s)" in ln:
            print("    " + ln.strip())
    if "0 Error(s)" not in txt:
        print()
        print("  X 编译有错误，先在 Keil 里改好")
        input("\n按回车退出...")
        sys.exit(1)

# ---------- 转 bin ----------
print()
print(LINE)
print("  [2/4] 生成 .bin")
print(LINE)
axf = os.path.join(projdir, "Objects", outname + ".axf")
binf = os.path.join(projdir, "Objects", outname + ".bin")
if not os.path.exists(axf):
    print("    X 找不到 %s" % axf)
    input("\n按回车退出...")
    sys.exit(1)
subprocess.run([FROMELF, "--bin", "--output", binf, axf],
               cwd=os.path.dirname(axf), capture_output=True)
if os.path.exists(binf):
    print("    OK %s (%.1f KB)" % (os.path.basename(binf), os.path.getsize(binf) / 1024))
else:
    print("    X fromelf 失败")
    input("\n按回车退出...")
    sys.exit(1)

# ---------- 找串口 ----------
print()
print(LINE)
print("  [3/4] 找串口")
print(LINE)
port = find_ch340()
if not port:
    print("    X 没找到 CH340（USB-TTL）")
    print("      -> USB-TTL TXD -> STM32【A10】")
    print("         USB-TTL RXD -> STM32【A9】")
    print("         USB-TTL GND -> STM32【G】")
    input("\n按回车退出...")
    sys.exit(1)
print("    OK %s" % port)

# ---------- ★★★ 检查串口是否被占用 ----------
print()
print("  ★ 检查串口有没有被别的程序占着...")
port_busy = False
for try_n in range(1, 4):
    try:
        _t = serial.Serial(port, 9600, timeout=0.1)
        _t.close()
        print("    OK 串口空闲，可以使用")
        break
    except Exception as e:
        if try_n < 3:
            print("    第 %d 次打不开（%s），1 秒后重试..." % (try_n, str(e)[:40]))
            time.sleep(1.0)
        else:
            port_busy = True

if port_busy:
    print()
    print(LINE)
    print("  ★★★ 串口被占用了！烧录脚本打不开 %s" % port)
    print(LINE)
    print()
    print("  ★★ 这是烧录失败的常见原因！请检查：")
    print()
    print("     1. ★ serial.bat（串口助手）还开着吗？ → 关掉它")
    print("     2. 别的 K 窗口 / 串口调试助手还开着吗？ → 都关掉")
    print("     3. 上一次的 flash_any.bat 窗口没关？    → 关掉")
    print()
    print("  ★ 关掉之后再重新运行本脚本")
    print()
    input("按回车退出...")
    sys.exit(1)

# ---------- 烧录 ----------
print()
print(LINE)
print("  [4/4] 烧录")
print(LINE)
print()
print("  ★★ 请先做好：")
print("       1. BOOT0 跳线帽拨到【1】那边")
print("       2. 按一下复位键（RST）")
print()
print("  ★ 接下来（★ v6 快了很多）：")
print("       屏幕会飞快地试（每 50 毫秒一次），你【看着屏幕按复位键】")
print("       一旦握上手，立刻自动开始烧录")
print()
input("  做好了按回车，开始...")
print()

# ==================== 阶段 1：握手（★ v6 快速方式） ====================
print(LINE)
print("  [1/2] 找连接（★ 保持串口打开，连续快速试探）")
print(LINE)
print()

# ★★★ 2026-10-06 用户实测：19200 是【能成功的最快波特率】，排主力
#   （19200 烧 27KB 约 15 秒；9600 要约 30 秒）
PLAN = [
    (38400, 100),    # ★★★ 主力：用户实测最快、能成功（烧 27KB 约 7 秒）
    (19200, 80),     # ★ 第二选择（约 15 秒）
    (9600,  60),     # 第三选择（约 30 秒）
]

good_baud = None
for baud, tries in PLAN:
    if good_baud:
        break
    print("  ┌──────────────────────────────────────────┐")
    print("  │  试波特率 %-6d （最多 %3d 次，每次 35ms）  │" % (baud, tries))
    print("  └──────────────────────────────────────────┘")
    print("     ★ 请现在开始反复按 STM32 的复位键...")
    print()

    ok, n = try_sync_fast(port, baud, max_tries=tries)

    if ok:
        good_baud = baud
    else:
        print("     X 这个波特率试了 %d 次没成功，换下一个" % tries)
        print()

if not good_baud:
    print()
    print(LINE)
    print("  X 所有波特率都没连上")
    print(LINE)
    print()
    print("  检查：")
    print("    1. BOOT0 是不是在【1】")
    print("    2. 按过复位键吗（★ 边看屏幕边按，不要停）")
    print("    3. USB-TTL 三根线（TXD->A10、RXD->A9、GND->G）")
    print("       试试把 TXD/RXD 对调")
    print("    4. 模块的线全拔掉（只留 USB-TTL）")
    print("    5. 杜邦线换成短的")
    input("\n按回车退出...")
    sys.exit(1)

print()
print("  ★ 握手成功！可用波特率 = %d" % good_baud)
print()

# ==================== 阶段 2：烧录 ====================
print(LINE)
print("  [2/2] 开始烧录（★ 别按复位、别碰线）")
print(LINE)
print()

ok = False
used = None

# ★ v6.2：先用握手成功的波特率，失败再依次降速
cands = [good_baud] + [x for x in (38400, 19200, 9600, 4800) if x < good_baud]

for b in cands:
    print("  ---- 试波特率 %d ----" % b)
    try:
        r = subprocess.run(
            [sys.executable, "-m", "stm32loader", "-p", port, "-b", str(b),
             "-e", "-w", "-v", binf, "-g", "0x08000000"],
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
    print("    X 失败，换下一个波特率")
    print()

# ---------- 结束 ----------
print()
print(LINE)
if ok:
    print("  ★★★ 烧录成功！（波特率 %d）" % used)
    print(LINE)
    print()
    print("  ★ 现在做这两步：")
    print("       1. BOOT0 跳线帽拨回【0】")
    print("       2. 按一下复位键")
    print()
    print("  ★ 然后双击 serial.bat 看串口打印")
else:
    print("  X 烧录失败")
    print(LINE)
    print()
    print("  排查：")
    print("    1. BOOT0 是否真的在【1】")
    print("    2. USB-TTL 三根线，试试 TXD/RXD 对调")
    print("    3. 杜邦线换短的")
    print("    4. 模块的线全部拔掉")
print()
input("按回车退出...")
