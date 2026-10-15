#!/usr/bin/env python3
"""
extract_company_ids.py — 从 Bluetooth SIG《Assigned Numbers》PDF 抽取公司标识表。

用法:
    python tools/extract_company_ids.py "docs/Assigned_Numbers (1).pdf"
    python tools/extract_company_ids.py <pdf> -o company_ids.csv
    python tools/extract_company_ids.py <pdf> --dump-text company_ids_raw.txt

依赖（任选其一，优先 pdfplumber）:
    pip install pdfplumber --break-system-packages
    # 或
    pip install pypdf --break-system-packages

输出:
    company_ids.csv        形如 "0x004C,Apple, Inc."，可直接喂给 gen_company_db.py
    <dump>                 （--dump-text 时）公司标识段及其后的原始文本，供人工核对

设计说明:
    SIG 文档版本间排版不一，公司 ID 可能是 0x 十六进制或十进制，表头行、
    分页页眉/页脚会混入。本脚本：定位 “Company Identifier” 锚点 → 逐行尝试
    两种 ID 正则 → 取命中更多的那个 → 跳过表头/页眉 → 落 CSV。
    若结果不对，用 --dump-text 把原始段发出去再调。
"""
import argparse
import csv
import re
import sys

# --- 每行: 首列是 ID，其余是公司名 ---------------------------------------
RE_HEX = re.compile(r"^\s*(0x[0-9A-Fa-f]{1,4})\s+(.+?)\s*$")
RE_DEC = re.compile(r"^\s*(\d{1,5})\s+(?!\d)(.+?)\s*$")

# 锚点：公司标识段起始
ANCHOR_RE = re.compile(r"company\s+identifier", re.I)
# 段结束：下一个大节标题（出现即停）
STOP_RE = re.compile(
    r"^\s*(service\s+and\s+characteristic|characteristic\s+declarations|"
    r"object\s+types?|gatt\s+service|declarations|service\s+uuid|"
    r"member\s+companies|unicode)\b",
    re.I,
)
# 需跳过的表头/页眉/页脚行
SKIP_RE = re.compile(r"^\s*(company\s+id|company\s+name|page\s+\d+|"
                     r"\d+\s*$|bluetooth\s+sig)", re.I)


def extract_text(pdf_path):
    """返回整份 PDF 的文本（pages 用换行连接）。优先 pdfplumber，退 pypdf。"""
    try:
        import pdfplumber
        parts = []
        with pdfplumber.open(pdf_path) as pdf:
            for pg in pdf.pages:
                parts.append(pg.extract_text() or "")
        print("[i] 使用 pdfplumber 提取", file=sys.stderr)
        return "\n".join(parts)
    except ImportError:
        pass

    try:
        from pypdf import PdfReader
    except ImportError:
        try:
            from PyPDF2 import PdfReader  # 老包名
        except ImportError:
            print("错误：需要 pdfplumber 或 pypdf。安装：\n"
                  "    pip install pdfplumber --break-system-packages", file=sys.stderr)
            sys.exit(2)

    print("[i] 使用 pypdf 提取", file=sys.stderr)
    reader = PdfReader(pdf_path)
    return "\n".join((p.extract_text() or "") for p in reader.pages)


def slice_section(text):
    """截取公司标识段。返回 (lines, start_line_no)。"""
    lines = text.splitlines()
    start = None
    for i, ln in enumerate(lines):
        if ANCHOR_RE.search(ln) and len(ln) < 80:  # 避免目录项误命中
            start = i
            break
    if start is None:
        # 找不到锚点时，整篇都试（并在 stderr 警告）
        print("[w] 未找到 'Company Identifier' 锚点，改用全文匹配", file=sys.stderr)
        start = 0

    out = []
    blank_run = 0
    for i in range(start, len(lines)):
        ln = lines[i]
        if STOP_RE.match(ln):
            break
        out.append(ln)
        if ln.strip() == "":
            blank_run += 1
            if blank_run > 40:      # 连续大量空行，认为段结束
                break
        else:
            blank_run = 0
    return out, start


def parse_entries(lines, regex):
    """按给定正则解析，返回 [(id_int, name), ...] 及命中行数。"""
    rows = []
    for ln in lines:
        if not ln.strip():
            continue
        m = regex.match(ln)
        if not m:
            continue
        tok, name = m.group(1), m.group(2).strip()
        if SKIP_RE.match(tok) or SKIP_RE.match(name):
            continue
        try:
            cid = int(tok, 16) if tok.lower().startswith("0x") else int(tok)
        except ValueError:
            continue
        if cid <= 0 or cid > 0xFFFF:
            continue
        if not name:
            continue
        rows.append((cid, name))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pdf_path")
    ap.add_argument("-o", "--out", default="company_ids.csv")
    ap.add_argument("--dump-text", default=None,
                    help="把公司标识段的原始文本写到该文件以供核对")
    args = ap.parse_args()

    text = extract_text(args.pdf_path)
    section, start = slice_section(text)
    print(f"[i] 公司标识段：起始行 {start}，共 {len(section)} 行", file=sys.stderr)

    if args.dump_text:
        with open(args.dump_text, "w", encoding="utf-8") as f:
            f.write("\n".join(section))
        print(f"[i] 原始段已写入 {args.dump_text}", file=sys.stderr)

    hex_rows = parse_entries(section, RE_HEX)
    dec_rows = parse_entries(section, RE_DEC)
    print(f"[i] 十六进制正则命中 {len(hex_rows)} 条；十进制正则命中 {len(dec_rows)} 条",
          file=sys.stderr)

    rows = hex_rows if len(hex_rows) >= len(dec_rows) else dec_rows
    if not rows:
        # 兜底：全文再试一次
        print("[w] 段内无命中，改用全文匹配", file=sys.stderr)
        rows = parse_entries(text.splitlines(), RE_HEX)
        if not rows:
            rows = parse_entries(text.splitlines(), RE_DEC)

    # 去重（同 ID 保留首个）
    seen = {}
    for cid, name in rows:
        seen.setdefault(cid, name)
    rows = sorted(seen.items())

    if not rows:
        print("错误：没有解析到任何条目。请加 --dump-text 生成原始段并发来核对。",
              file=sys.stderr)
        sys.exit(1)

    with open(args.out, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["Company ID", "Company Name"])
        for cid, name in rows:
            w.writerow(["0x%04X" % cid, name])

    print(f"\n已写出 {args.out}：{len(rows)} 条", file=sys.stderr)
    print("前 8 条预览：", file=sys.stderr)
    for cid, name in rows[:8]:
        print("    0x%04X  %s" % (cid, name), file=sys.stderr)
    print("含 Apple(0x004C)? %s" % any(c == 0x004C for c, _ in rows), file=sys.stderr)
    print("\n下一步：\n    python tools/gen_company_db.py %s -o main/identify/company_db_data.inc"
          % args.out, file=sys.stderr)


if __name__ == "__main__":
    main()
