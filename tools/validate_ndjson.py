#!/usr/bin/env python3
"""
校验工具：读串口或文件，把每一行按 NDJSON 校验。

兼容两种输出形态（P1 逐包 / P2 设备汇总）：
  - 逐包: {"t":..,"evt":"ADV_IND","addr":..,"rssi":..,"co":..}
  - 汇总: {"snap":..} 摘要行；{"dev":true,"addr":..,"co":..,"subtype":..} 设备行

用法：
    python tools/validate_ndjson.py --file capture.log
    python tools/validate_ndjson.py --port COM5 --expect-co 76
    python tools/validate_ndjson.py --file capture.log --dump-devices

退出码：0 = 全部合法；1 = 存在解析失败。
"""
import argparse
import json
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    serial = None

# 行类型 → 必需字段
REQUIRED_BY_KIND = {
    "packet": ("t", "evt", "addr", "rssi"),
    "dev":    ("addr", "co"),
    "snap":   ("devices",),
}

# 允许出现的非 JSON 行前缀（芯片 ROM 引导器等）
ALLOW_PREFIXES = ("ESP-ROM:", "Build:", "rst:", "SPI mode:",
                  "load:", "entry ", "[boot]", "--- ", "Warning:",
                  "I (", "W (", "E (")


def classify(obj):
    if obj.get("snap") is not None:
        return "snap"
    if obj.get("dev"):
        return "dev"
    return "packet"


def validate_line(line: str):
    """返回 (ok, obj_or_None, err)。空行/白名单行返回 (None, None, None)。"""
    s = line.strip()
    if not s:
        return None, None, None
    if s.startswith(ALLOW_PREFIXES):
        return None, None, None
    try:
        obj = json.loads(s)
    except json.JSONDecodeError as e:
        return False, None, f"JSON 解析失败: {e}"
    if not isinstance(obj, dict):
        return False, None, "顶层不是 JSON 对象"
    req = REQUIRED_BY_KIND[classify(obj)]
    missing = [k for k in req if k not in obj]
    if missing:
        return False, obj, f"缺字段: {missing}"
    return True, obj, None


def run(iter_lines, expect_co=None, dump_devices=False):
    total = ok = bad = 0
    apple = 0
    snaps = 0
    devices = {}          # addr -> obj（汇总行）
    for line in iter_lines:
        res, obj, err = validate_line(line)
        if res is None:
            continue
        total += 1
        if not res:
            bad += 1
            print(f"[BAD] {err}: {line.strip()[:120]}", file=sys.stderr)
            continue
        ok += 1
        kind = classify(obj)
        if kind == "snap":
            snaps += 1
        elif kind == "dev":
            devices[obj.get("addr")] = obj
        if expect_co is not None and obj.get("co") == expect_co:
            apple += 1
            if kind == "packet":
                print(f"[Apple? co={expect_co}] {line.strip()}")
    print(f"\n总计 {total} 行：合法 {ok}，非法 {bad}", file=sys.stderr)
    print(f"快照批数 {snaps}，唯一设备 {len(devices)}", file=sys.stderr)
    if expect_co is not None:
        print(f"匹配 co=={expect_co} 的行：{apple}", file=sys.stderr)
    if dump_devices and devices:
        print("\n设备表:", file=sys.stderr)
        for a, o in sorted(devices.items()):
            print(f"  {a}  co={o.get('co')} vendor={o.get('vendor')!r} "
                  f"subtype={o.get('subtype')!r} tracker={o.get('tracker')!r} "
                  f"n={o.get('n')}", file=sys.stderr)
    return 0 if bad == 0 else 1


def main():
    ap = argparse.ArgumentParser()
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="串口，如 COM5 或 /dev/ttyUSB0")
    src.add_argument("--file", help="已抓取的日志文件")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--expect-co", type=int, default=None,
                    help="统计并打印该 Company ID 的行，如 76 (Apple)")
    ap.add_argument("--dump-devices", action="store_true",
                    help="结束时打印去重后的设备表")
    args = ap.parse_args()

    if args.file:
        with open(args.file, "r", encoding="utf-8", errors="replace") as f:
            return run(f, args.expect_co, args.dump_devices)

    if serial is None:
        print("需要 pyserial：pip install pyserial --break-system-packages", file=sys.stderr)
        return 2
    ser = serial.Serial(args.port, args.baud, timeout=1)
    print(f"读取 {args.port} @ {args.baud} …… Ctrl-C 结束", file=sys.stderr)

    def gen():
        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                yield raw.decode("utf-8", errors="replace")
        except KeyboardInterrupt:
            ser.close()
            return

    return run(gen(), args.expect_co, args.dump_devices)


if __name__ == "__main__":
    sys.exit(main())

