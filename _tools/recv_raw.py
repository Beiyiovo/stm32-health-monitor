r"""501 数据抓取与 CRC 对拍工具

用途：
  1. 连 ESP32 的 TCP 服务端（192.168.4.1:8080），接收 STM32 上报的原始帧
  2. 用独立的 Python 实现重算 CRC8，与固件帧尾的校验值逐帧比对（E6 对拍）
  3. 打印每一帧的十六进制与解析后的字段，便于肉眼核对
  4. 统计收到帧数、CRC 正确/错误数、超时与断线次数

使用前提：
  电脑连上热点  HealthHelper  （密码 12345678）
  板子上电，OLED 正常显示

用法：
  python recv_raw.py              # 默认 192.168.4.1:8080，一直收
  python recv_raw.py --time 60    # 收 60 秒后自动停
  python recv_raw.py --hex        # 同时打印每帧的十六进制原文

帧格式（来自固件 main.c:209）：
  $HR=76,SPO2=97,TEMP=25.3,STEP=4,SLEEP=0,SDUR=0,UP=3600*3F\r\n
  CRC-8, polynomial=0x07, init=0x00，覆盖 $ 到最后一个字段值（不含 * ）
"""
import argparse
import socket
import sys
import time

HOST_DEFAULT = '192.168.4.1'
PORT_DEFAULT = 8080


def crc8(data: bytes) -> int:
    """与固件 crc8_calc() 完全一致的实现：poly=0x07, init=0x00"""
    crc = 0x00
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def parse_frame(line: bytes):
    """返回 (ok, crc_got, crc_calc, fields_dict, raw_body)"""
    if not line.startswith(b'$'):
        return None
    star = line.rfind(b'*')
    if star < 0 or star + 3 > len(line):
        return None
    body = line[:star]
    try:
        got = int(line[star + 1:star + 3], 16)
    except ValueError:
        return None
    calc = crc8(body)
    fields = {}
    for part in body[1:].decode('ascii', 'replace').split(','):
        if '=' in part:
            k, v = part.split('=', 1)
            fields[k] = v
    return (got == calc, got, calc, fields, body)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default=HOST_DEFAULT)
    ap.add_argument('--port', type=int, default=PORT_DEFAULT)
    ap.add_argument('--time', type=float, default=0, help='运行秒数，0=一直跑')
    ap.add_argument('--hex', action='store_true', help='打印每帧十六进制')
    ap.add_argument('--reconnect', action='store_true', default=True)
    args = ap.parse_args()

    t_start = time.time()
    n_ok = n_bad = n_noframe = 0
    n_conn = 0
    samples = []

    print('=' * 60)
    print(' 目标 %s:%d   按 Ctrl+C 停止' % (args.host, args.port))
    print('=' * 60)

    while True:
        if args.time and (time.time() - t_start) > args.time:
            break
        try:
            s = socket.create_connection((args.host, args.port), timeout=8)
            n_conn += 1
            print('\n[连接成功 #%d] %s' % (n_conn, time.strftime('%H:%M:%S')))
            s.settimeout(10)
            buf = b''
            while True:
                if args.time and (time.time() - t_start) > args.time:
                    raise KeyboardInterrupt
                try:
                    chunk = s.recv(4096)
                except socket.timeout:
                    print('  [超时 10 s 无数据，准备重连]')
                    break
                if not chunk:
                    print('  [对端关闭，准备重连]')
                    break
                buf += chunk
                while b'\n' in buf:
                    raw, buf = buf.split(b'\n', 1)
                    raw = raw.rstrip(b'\r')
                    if not raw:
                        continue
                    r = parse_frame(raw)
                    if r is None:
                        n_noframe += 1
                        print('  [非数据行] %r' % raw[:80])
                        continue
                    ok, got, calc, f, body = r
                    if ok:
                        n_ok += 1
                        if len(samples) < 200:
                            samples.append(raw)
                        print('  ✓ %s  [CRC 0x%02X 正确]' % (
                            ' '.join('%s=%s' % (k, v) for k, v in f.items()), got))
                        if args.hex:
                            print('     hex: %s' % body.hex(' '))
                    else:
                        n_bad += 1
                        print('  ✗ CRC 不符！帧尾 0x%02X  实算 0x%02X' % (got, calc))
                        print('     raw: %s' % raw.decode('ascii', 'replace'))
        except KeyboardInterrupt:
            break
        except OSError as e:
            print('  [连接失败] %s  （3 s 后重试）' % e)
            time.sleep(3)

    dur = time.time() - t_start
    print()
    print('=' * 60)
    print(' 统计')
    print('=' * 60)
    print(' 运行时长     : %.1f s' % dur)
    print(' 建立连接     : %d 次' % n_conn)
    print(' CRC 正确帧   : %d' % n_ok)
    print(' CRC 错误帧   : %d' % n_bad)
    print(' 非数据行     : %d' % n_noframe)
    if n_ok + n_bad:
        print(' 校验通过率   : %.2f%%' % (100.0 * n_ok / (n_ok + n_bad)))
    if samples:
        print()
        print(' 最后一帧原文 :')
        print('   %s' % samples[-1].decode('ascii', 'replace'))
    print()


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\n[用户中断]')
