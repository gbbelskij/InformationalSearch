#!/usr/bin/env python3
import filecmp
import os
import subprocess

from common import DOCS, INDEXER, LAB7, MIB, RAW_INDEX, RESULTS, ROOT, VB_INDEX, kv, loadavg, num, table


def index(args, path):
    with open(DOCS, encoding="utf-8") as f:
        res = subprocess.run([INDEXER, *args, path], stdin=f, capture_output=True, text=True, check=True,
                             cwd=LAB7)
    return {k: float(v) for k, v in kv(res.stdout).items()}


ORDER = ["raw", "vb", "vb", "raw"]
TIMES = ["time_s", "read_s", "index_s", "sort_s", "write_s", "peak_rss_mb"]


def best(runs):
    out = dict(runs[0])
    for r in runs[1:]:
        for k in TIMES:
            out[k] = min(out[k], r[k])
    return out


def fmt(v):
    return f"{v:.0f}" if float(v).is_integer() else f"{v:.3f}"


def main():
    os.makedirs(os.path.dirname(VB_INDEX), exist_ok=True)
    tmp = os.path.join(ROOT, "data", "index-raw.tmp")
    load = loadavg()
    runs = {"raw": [], "vb": []}
    same = True
    for variant in ORDER:
        if variant == "raw":
            runs["raw"].append(index([], tmp))
            same = same and os.path.exists(RAW_INDEX) and filecmp.cmp(tmp, RAW_INDEX, shallow=False)
            os.remove(tmp)
        else:
            runs["vb"].append(index(["-c"], VB_INDEX))
    raw, vb = best(runs["raw"]), best(runs["vb"])
    keys = ["docs", "postings", "postings_bytes", "index_bytes", "time_s", "read_s", "index_s", "sort_s", "write_s",
            "peak_rss_mb"]
    with open(os.path.join(RESULTS, "indexing.tsv"), "w", encoding="utf-8") as f:
        f.write(f"# loadavg_before {load}; order {' '.join(ORDER)}, min of {len(ORDER) // 2} runs per phase; "
                f"raw index equals lab7/data/index.bin: {int(same)}\n")
        f.write("key\traw\tvbyte\n")
        for k in keys:
            f.write(f"{k}\t{fmt(raw[k])}\t{fmt(vb[k])}\n")
        for i, (a, b) in enumerate(zip(runs["raw"], runs["vb"]), 1):
            f.write(f"run{i}_time_s\t{a['time_s']:.3f}\t{b['time_s']:.3f}\n")
    rows = [
        ["Размер индекса, МиБ", num(raw["index_bytes"] / MIB, 1), num(vb["index_bytes"] / MIB, 1)],
        ["\\quad из них списки документов", num(raw["postings_bytes"] / MIB, 1), num(vb["postings_bytes"] / MIB, 1)],
        ["Время индексации (минимум), с", num(raw["time_s"], 1), num(vb["time_s"], 1)],
        ["\\quad чтение файлов", num(raw["read_s"], 1), num(vb["read_s"], 1)],
        ["\\quad токенизация и вставка в словарь", num(raw["index_s"], 1), num(vb["index_s"], 1)],
        ["\\quad сортировка словаря", num(raw["sort_s"], 2), num(vb["sort_s"], 2)],
        ["\\quad запись индекса (со сжатием списков)", num(raw["write_s"], 2), num(vb["write_s"], 2)],
        ["Пиковая память, МиБ", num(raw["peak_rss_mb"]), num(vb["peak_rss_mb"])],
    ]
    table("indexing-table.tex", ["Показатель", "Без сжатия", "VByte"], rows, "p{0.5\\textwidth}|r|r|")
    print(open(os.path.join(RESULTS, "indexing.tsv"), encoding="utf-8").read())


if __name__ == "__main__":
    main()
