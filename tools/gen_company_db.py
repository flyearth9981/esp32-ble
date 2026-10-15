#!/usr/bin/env python3
"""
gen_company_db.py — 从 Bluetooth SIG 官方「公司标识」CSV 生成 C 表。

用法:
    python tools/gen_company_db.py <input.csv>
    python tools/gen_company_db.py <input.csv> -o main/identify/company_db_data.inc

输入 CSV 需为 SIG 分配表导出格式，形如（列名可有可无，取第 1、2 列）:
    Company ID,Company Name
    0x0000,Reserved
    0x0001,"Nokia Mobile Phones"
    0x004C,"Apple, Inc."

下载来源（自行获取，注意许可）：
    https://www.bluetooth.com/specifications/assigned-numbers/  → Company Identifiers (CSV)

输出 .inc 文件由 company_db.c 用 #include 引入，替换 fallback 表。
生成为静态数组，存 flash（.rodata），不在 RAM。
"""
import argparse
import csv
import re
import sys

# 驼峰边界补空格：小写/数字 后接 大写（含连续大写段如 "IBM" 不拆）
_CAMEL = re.compile(r"(?<=[a-z0-9])(?=[A-Z])")


def fix_spacing(name, enable):
    """PDF 抽取常吃掉空格（NokiaMobilePhones）。在驼峰边界补空格。
    仅当名字里完全没有空格时启用，避免破坏 "Apple, Inc." 这类已正常的串。"""
    if not enable or " " in name:
        return name
    return _CAMEL.sub(" ", name)


def parse_id(s):
    s = s.strip()
    if not s:
        return None
    try:
        return int(s, 16) if s.lower().startswith("0x") else int(s, 0)
    except ValueError:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv_path")
    ap.add_argument("-o", "--out", default="company_db_data.inc")
    ap.add_argument("--raw-names", action="store_true",
                    help="不补空格，原样输出（默认会尝试修 PDF 吃掉空格的名字）")
    args = ap.parse_args()

    rows = []
    with open(args.csv_path, newline="", encoding="utf-8-sig") as f:
        reader = csv.reader(f)
        for row in reader:
            if len(row) < 2:
                continue
            cid = parse_id(row[0])
            if cid is None or cid == 0:      # 跳过表头/保留项
                continue
            name = row[1].strip()
            if not name:
                continue
            name = fix_spacing(name, not args.raw_names)
            rows.append((cid, name))

    if not rows:
        print("错误：没有解析到任何条目，检查 CSV 格式", file=sys.stderr)
        sys.exit(1)

    rows.sort(key=lambda r: r[0])

    with open(args.out, "w", encoding="utf-8") as f:
        f.write("/* 本文件由 tools/gen_company_db.py 自动生成，请勿手改。 */\n")
        f.write("static const cid_entry_t cid_table[] = {\n")
        for cid, name in rows:
            esc = name.replace("\\", "\\\\").replace('"', '\\"')
            f.write('    {0x%04X, "%s"},\n' % (cid, esc))
        f.write("};\n")

    print("已生成 %s：%d 条厂商记录" % (args.out, len(rows)))


if __name__ == "__main__":
    main()
