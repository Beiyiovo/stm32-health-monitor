# -*- coding: utf-8 -*-
"""
501 心率算法选择器（中文界面）

用法：双击 选算法.bat
"""
import io, os, shutil, sys, time
sys.stdout.reconfigure(encoding="utf-8")

# ★ 2026-10-12：改成相对脚本自身定位，clone 到任何目录都能跑（以前写死了作者的 T 盘路径）
BASE = os.path.dirname(os.path.abspath(__file__))
OBJ = os.path.join(BASE, "09_main", "Objects")

ALGOS = [
    ("1", "peak",    "方案1  用【峰】计心率",
     "保底方案。在脉搏波形上找波峰，用两次波峰的时间差算心率。",
     "实测心率偏高 15~20%（真实 65 会显示成 78~90）",
     "原因：重搏波（主峰后面的小峰）会被误当成一次心搏"),
    ("2", "valley",  "方案2  用【谷】计心率",
     "在脉搏波形上找波谷。每个心搏周期只有一个真正的谷。",
     "预期接近真实值（60~80）",
     "优点：重搏波不会产生额外的谷，天然被跳过"),
    ("3", "hamming", "方案3  汉明窗卷积法",
     "先对波形求微分，再用汉明窗做匹配滤波（Maxim 官方 MAXREFDES117 的思路）。",
     "预期最抗噪声",
     "优点：重搏波是缓慢隆起，不产生陡峭下降沿，卷积后几乎不出现"),
]

def main():
    os.system("cls" if os.name == "nt" else "clear")
    print()
    print("=" * 66)
    print("      501 心率算法 —— 选择器（三个方案已编译好）")
    print("=" * 66)
    print()

    for num, key, title, desc, expect, reason in ALGOS:
        p = os.path.join(OBJ, "step9_%s.bin" % key)
        ok = "有" if os.path.exists(p) else "★ 缺"
        print("  [%s] %s       固件:%s" % (num, title, ok))
        print("       %s" % desc)
        print("       预期：%s" % expect)
        print("       原理：%s" % reason)
        print()

    print("-" * 66)
    print("  当前正在用的是哪个？看下面这个文件：")
    cur = os.path.join(OBJ, "step9_main.bin")
    if os.path.exists(cur):
        import hashlib

        def md5(p):
            return hashlib.md5(open(p, "rb").read()).hexdigest()

        h = md5(cur)
        who = []
        for num, key, title, *_ in ALGOS:
            p = os.path.join(OBJ, "step9_%s.bin" % key)
            if os.path.exists(p) and md5(p) == h:
                who.append(title.replace("  ", " "))
        print("     →  %s" % ("   ".join(who) if who else "未知（可能被手动改过）"))
    print()

    # 让用户选
    print("-" * 66)
    while True:
        sel = input("  请输入 1 / 2 / 3 选择要烧的方案（直接回车 = 2）：").strip()
        if sel == "":
            sel = "2"
        if sel in ("1", "2", "3"):
            break
        print("  ★ 只能输 1、2、3")

    key = dict((a[0], a[1]) for a in ALGOS)[sel]
    src = os.path.join(OBJ, "step9_%s.bin" % key)
    if not os.path.exists(src):
        print()
        print("  X 找不到 %s，请先编译。" % src)
        input("  按回车退出...")
        return

    shutil.copy(src, cur)
    title = dict((a[0], a[2]) for a in ALGOS)[sel]
    print()
    print("  " + "=" * 62)
    print("   ✅ 已切换到：%s" % title)
    print("  " + "=" * 62)
    print()
    print("  现在按这个顺序烧录：")
    print()
    print("     ① 关掉 serial.bat（如果开着）")
    print("     ② STM32 的 BOOT0 跳到 1，按一下复位")
    print("     ③ 双击 flash_any.bat，输入 9，回车")
    print("     ④ 烧完后 BOOT0 跳回 0，按一下复位")
    print("     ⑤ 双击 serial.bat")
    print("     ⑥ ★ 手指放上去，保持 40 秒，记录心率数值")
    print()
    print("  ★ 测完这个方案后，再运行一次本脚本选下一个，重复 ①~⑥")
    print("  ★ 三个都测完，把三组数据放一起对比，选最接近真实值的")
    print()
    input("  按回车退出...")


if __name__ == "__main__":
    main()
