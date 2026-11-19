#!/usr/bin/env python3
import glob
import itertools
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
BIN = os.path.join(ROOT, "build", "tokenizer")
CORPUS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "..", "lab1", "data_1m", "text")
SIZES = [1000, 3000, 10000, 30000, 100000, 300000, 1000000]
ORDER = ("wikivoyage", "wikipedia", "travelse", "reddit")
NAMES = {"wikivoyage": "Wikivoyage", "wikipedia": "Wikipedia", "travelse": "Travel SE", "reddit": "Reddit"}
REPEAT = 3


def run(paths):
    res = subprocess.run([BIN, "-s"], input="\n".join(paths) + "\n", capture_output=True,
                         text=True, check=True)
    out = {}
    for line in res.stdout.splitlines():
        key, value = line.split("\t")
        out[key] = float(value)
    return out


def num(v, digits=0):
    return f"{v:,.{digits}f}".replace(",", "\\,").replace(".", ",")


def table(path, head, rows):
    with open(path, "w", encoding="utf-8") as f:
        f.write("\\begin{tabular}{|l|" + "r|" * (len(head) - 1) + "}\n\\hline\n")
        f.write(" & ".join(head) + " \\\\\n\\hline\n")
        for r in rows:
            f.write(" & ".join(r) + " \\\\\n\\hline\n")
        f.write("\\end{tabular}\n")


def stats_by_source(sources, mixed):
    rows, tsv = [], ["source\tfiles\tbytes\ttokens\tavg_len"]
    for name, paths in list(sources.items()) + [("Итого", mixed)]:
        s = run(paths)
        tsv.append(f"{name}\t{s['files']:.0f}\t{s['bytes']:.0f}\t{s['tokens']:.0f}\t{s['avg_len']:.3f}")
        rows.append([NAMES.get(name, name), num(s["files"]), num(s["bytes"] / 2**20, 1),
                     num(s["tokens"]), num(s["tokens"] / s["files"]), num(s["avg_len"], 2)])
    with open(os.path.join(ROOT, "results", "stats.tsv"), "w", encoding="utf-8") as f:
        f.write("\n".join(tsv) + "\n")
    print("\n".join(tsv) + "\n")
    table(os.path.join(ROOT, "report", "src", "stats-table.tex"),
          ["Источник", "Док.", "Текст, МиБ", "Токенов", "Токенов на док.", "Ср. длина токена"], rows)


def timing(mixed, merged):
    sizes = [n for n in SIZES if n < len(mixed)] + [len(mixed)]
    jobs = [(num(n), str(n), mixed[:n]) for n in sizes] + [("1 (склеенный)", "1(merged)", [merged])]
    best = [None] * len(jobs)
    for _ in range(REPEAT):
        for i, (_, _, paths) in enumerate(jobs):
            s = run(paths)
            if best[i] is None or s["time_s"] < best[i]["time_s"]:
                best[i] = s
            time.sleep(0.5)
    rows, tsv = [], ["files\tkib\ttime_s\tread_s\ttokenize_s\tus_per_kb\tkb_per_s"]
    for (label, key, _), s in zip(jobs, best):
        kib = s["bytes"] / 1024
        tsv.append(f"{key}\t{kib:.0f}\t{s['time_s']:.3f}\t{s['read_s']:.3f}\t{s['tokenize_s']:.3f}\t"
                   f"{s['time_s'] * 1e6 / kib:.2f}\t{kib / s['time_s']:.0f}")
        rows.append([label, num(kib / 1024, 1), num(s["time_s"], 2), num(s["read_s"], 2),
                     num(s["tokenize_s"], 2), num(s["time_s"] * 1e6 / kib, 1),
                     num(s["tokenize_s"] * 1e6 / kib, 1)])
    with open(os.path.join(ROOT, "results", "bench.tsv"), "w", encoding="utf-8") as f:
        f.write("\n".join(tsv) + "\n")
    print("\n".join(tsv))
    table(os.path.join(ROOT, "report", "src", "bench-table.tex"),
          ["Файлов", "МиБ", "Всего, с", "Чтение, с", "Токениз., с", "мкс/КиБ", "мкс/КиБ (токениз.)"], rows)


def main():
    found = sorted(d for d in os.listdir(CORPUS) if os.path.isdir(os.path.join(CORPUS, d)))
    names = [s for s in ORDER if s in found] + [s for s in found if s not in ORDER]
    sources = {name: sorted(glob.glob(os.path.join(CORPUS, name, "*.txt"))) for name in names}
    mixed = [p for row in itertools.zip_longest(*sources.values()) for p in row if p is not None]
    stats_by_source(sources, mixed)
    with tempfile.TemporaryDirectory() as tmp:
        merged = os.path.join(tmp, "corpus.txt")
        with open(merged, "wb") as out:
            for p in mixed:
                with open(p, "rb") as f:
                    out.write(f.read())
        timing(mixed, merged)


if __name__ == "__main__":
    main()
