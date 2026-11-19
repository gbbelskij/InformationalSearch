#!/usr/bin/env python3
import os
import re
import statistics
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SEARCH = os.path.join(ROOT, "build", "search")
LOOKUP = os.path.join(ROOT, "..", "lab7", "build", "lookup")
INDEX = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "..", "lab7", "data", "index.bin")
QUERIES = os.path.join(ROOT, "data", "queries.txt")
REPEAT = 50
PAIRS = [("museum", "art"), ("beach", "surfing"), ("castle", "palace"), ("lviv", "kyiv"), ("the", "zzzz")]


def blocks(text):
    out = []
    for chunk in text.split("\n\n"):
        lines = chunk.strip("\n").split("\n")
        if not lines or not lines[0].startswith("query:"):
            continue
        head = dict(line.split(": ", 1) if ": " in line else (line.rstrip(":"), "") for line in lines[:4])
        out.append(head)
    return out


def run(queries, *args):
    res = subprocess.run([SEARCH, INDEX, *args], input="".join(q + "\n" for q in queries),
                         capture_output=True, text=True, check=True)
    return res.stdout, res.stderr


def tex(s):
    return (s.replace("\\", "\\textbackslash{}").replace("&", "\\&").replace("|", "\\textbar{}")
            .replace("_", "\\_").replace("%", "\\%").replace("#", "\\#"))


def num(v, digits=0):
    return f"{v:,.{digits}f}".replace(",", "\\,").replace(".", ",")


def main():
    with open(QUERIES, encoding="utf-8") as f:
        queries = [line.rstrip("\n") for line in f if line.strip()]
    with open("/proc/loadavg") as f:
        load = f.read().split()[0]
    with open(INDEX, "rb") as f:
        total = struct.unpack_from("<I", f.read(16), 12)[0]

    loads, runs = [], []
    for _ in range(3):
        out, err = run(queries, "-n", "1", "-r", str(REPEAT))
        loads.append(float(re.search(r"([\d.]+) ms", err).group(1)))
        runs.append(blocks(out))
    best = [min(float(r[i]["time_us"]) for r in runs) for i in range(len(queries))]
    info = runs[0]

    with open(os.path.join(ROOT, "results", "bench.tsv"), "w", encoding="utf-8") as f:
        f.write(f"# loadavg_before {load}, index_load_ms {min(loads):.1f}, repeat {REPEAT}, min of 3 runs\n")
        f.write("query\tparsed\tfound\ttime_us\n")
        for q, b, t in zip(queries, info, best):
            f.write(f"{q}\t{b['parsed']}\t{b['found']}\t{t:.1f}\n")
    rows = "".join(f"{i} & \\texttt{{{tex(b['parsed'])}}} & {num(int(b['found']))} & {num(t, 1)} \\\\\n\\hline\n"
                   for i, (b, t) in enumerate(zip(info, best), 1))
    with open(os.path.join(ROOT, "report", "src", "bench-table.tex"), "w", encoding="utf-8") as f:
        f.write("\\begin{longtable}{|r|p{0.6\\textwidth}|r|r|}\n\\hline\n"
                "\\textnumero & Запрос после разбора & Найдено & мкс \\\\\n\\hline\n\\endhead\n" + rows +
                "\\end{longtable}\n")
    summary = (f"queries\t{len(queries)}\nmean_us\t{statistics.mean(best):.1f}\nmedian_us\t{statistics.median(best):.1f}\n"
               f"max_us\t{max(best):.1f}\nindex_load_ms\t{min(loads):.1f}\nloadavg_before\t{load}\n")
    with open(os.path.join(ROOT, "results", "bench-summary.tsv"), "w", encoding="utf-8") as f:
        f.write(summary)
    print(summary)

    out, _ = run(queries, "-n", "10")
    with open(os.path.join(ROOT, "results", "examples.txt"), "w", encoding="utf-8") as f:
        f.write(out)

    lines, ok = [], True
    for a, b in PAIRS:
        qs = [a, b, f"{a} {b}", f"{a} && {b}", f"{a} || {b}", f"!{a}", f"!({a} || {b})", f"!{a} !{b}", f"{a} !{b}"]
        n = [int(x["found"]) for x in blocks(run(qs, "-n", "1")[0])]
        docs = n[0] + n[5]
        df = [int(line.split("\t")[1]) for line in subprocess.run(
            [LOOKUP, INDEX], input=f"{a}\n{b}\n", capture_output=True, text=True, check=True).stdout.splitlines()]
        checks = [
            (n[0] == df[0] and n[1] == df[1], f"N({a}) = {n[0]}, N({b}) = {n[1]} — совпадают с df из lookup ЛР7"),
            (n[2] == n[3], f"N({a} {b}) = N({a} && {b}) = {n[3]}"),
            (n[3] <= min(n[0], n[1]), f"N({a} && {b}) <= min(N({a}), N({b}))"),
            (n[4] == n[0] + n[1] - n[3], f"N({a} || {b}) = {n[4]} = N({a}) + N({b}) - N({a} && {b})"),
            (docs == total, f"N({a}) + N(!{a}) = {docs} = числу документов"),
            (n[6] == n[7], f"N(!({a} || {b})) = N(!{a} !{b}) = {n[7]} (закон де Моргана)"),
            (n[8] == n[0] - n[3], f"N({a} !{b}) = {n[8]} = N({a}) - N({a} && {b})"),
        ]
        for cond, text in checks:
            lines.append(f"[{'ok' if cond else 'FAIL'}] {text}")
            ok = ok and cond
    lines.append(f"Итого: {'все проверки пройдены' if ok else 'есть ошибки'}")
    with open(os.path.join(ROOT, "results", "consistency.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
