#!/usr/bin/env python3
import os
import subprocess
import tempfile
import time

from common import DOCS, ROOT

INDEXER = os.path.join(ROOT, "..", "lab7", "build", "indexer")
STEM_INDEX = os.path.join(ROOT, "data", "index-stem.bin")
REPEAT = 3


def best(args):
    runs = []
    with open(DOCS, encoding="utf-8") as f:
        lines = f.read()
    for _ in range(REPEAT):
        out = subprocess.run([INDEXER, *args], input=lines, capture_output=True, text=True, check=True).stdout
        runs.append({k: float(v) for k, v in (line.split("\t") for line in out.splitlines())})
        time.sleep(0.5)
    return min(runs, key=lambda r: r["time_s"])


def fmt(v):
    return f"{v:.0f}" if v.is_integer() else f"{v}"


def num(v, digits=0):
    return f"{v:,.{digits}f}".replace(",", "\\,").replace(".", ",")


def main():
    with open("/proc/loadavg") as f:
        load = f.read().split()[0]
    with tempfile.TemporaryDirectory() as tmp:
        plain = best([os.path.join(tmp, "index.bin")])
    stem = best(["-s", STEM_INDEX])
    keys = ["terms", "postings", "avg_term_len", "index_bytes", "time_s", "peak_rss_mb"]
    with open(os.path.join(ROOT, "results", "index-stats.tsv"), "w", encoding="utf-8") as f:
        f.write(f"# loadavg_before {load}, min of {REPEAT} runs\nkey\tplain\tstem\n")
        f.write("".join(f"{k}\t{fmt(plain[k])}\t{fmt(stem[k])}\n" for k in keys))
    mib = 2 ** 20
    rows = [("Термов", num(plain["terms"]), num(stem["terms"]), plain["terms"], stem["terms"]),
            ("Записей в списках документов", num(plain["postings"]), num(stem["postings"]),
             plain["postings"], stem["postings"]),
            ("Средняя длина терма, символов", num(plain["avg_term_len"], 2), num(stem["avg_term_len"], 2),
             plain["avg_term_len"], stem["avg_term_len"]),
            ("Размер индекса, МиБ", num(plain["index_bytes"] / mib, 1), num(stem["index_bytes"] / mib, 1),
             plain["index_bytes"], stem["index_bytes"]),
            ("Время индексации, с", num(plain["time_s"], 2), num(stem["time_s"], 2), plain["time_s"], stem["time_s"])]
    with open(os.path.join(ROOT, "report", "src", "index-table.tex"), "w", encoding="utf-8") as f:
        f.write("\\begin{tabular}{|l|r|r|r|}\n\\hline\nПоказатель & Без стемминга & Со стеммингом & Изменение \\\\\n\\hline\n")
        for name, a, b, x, y in rows:
            f.write(f"{name} & {a} & {b} & {num(100 * (y - x) / x, 1)}\\,\\% \\\\\n\\hline\n")
        f.write("\\end{tabular}\n")
    print(f"loadavg {load}")
    for k in keys:
        print(f"{k}\t{fmt(plain[k])}\t{fmt(stem[k])}")


if __name__ == "__main__":
    main()
