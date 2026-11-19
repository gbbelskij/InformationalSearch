#!/usr/bin/env python3
import collections
import os
import subprocess
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
TOKENIZER = os.path.join(HERE, "..", "..", "lab3", "build", "tokenizer")
TABLE_RANKS = [1, 2, 3, 10, 100, 500, 1000, 10000, 100000, 1000000]
STEP = 1.03


def text_files(root):
    paths = []
    for d, _, files in os.walk(root):
        paths += [os.path.join(d, f) for f in files if f.endswith(".txt")]
    return sorted(paths)


def count(paths):
    proc = subprocess.Popen([TOKENIZER], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def feed():
        for p in paths:
            with open(p, "rb") as f:
                proc.stdin.write(f.read())
            proc.stdin.write(b"\n")
        proc.stdin.close()

    writer = threading.Thread(target=feed)
    writer.start()
    freq = collections.Counter(proc.stdout)
    writer.join()
    if proc.wait() != 0:
        sys.exit("zipf: tokenizer failed")
    return sorted(((line[:-1].decode("utf-8"), f) for line, f in freq.items()), key=lambda x: (-x[1], x[0]))


def plot_ranks(n):
    ranks, r = [], 1.0
    while r < n:
        if not ranks or int(r) != ranks[-1]:
            ranks.append(int(r))
        r *= STEP
    return ranks + [n]


def tex_term(term):
    return "".join(c if c.isascii() or 0xC0 <= ord(c) < 0x180 or 0x400 <= ord(c) < 0x460 else f"[U+{ord(c):04X}]"
                   for c in term)


def num(v, digits=0):
    return f"{v:,.{digits}f}".replace(",", "\\,").replace(".", ",")


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: zipf.py CORPUS_DIR OUT_DIR")
    corpus, out = sys.argv[1], sys.argv[2]
    paths = text_files(corpus)
    if not paths:
        sys.exit(f"zipf: no .txt files in {corpus}")
    terms = count(paths)
    if not terms:
        sys.exit("zipf: no tokens in corpus")
    os.makedirs(out, exist_ok=True)
    c = terms[0][1]
    tokens = sum(f for _, f in terms)
    hapax = sum(1 for _, f in terms if f == 1)

    with open(os.path.join(out, "freq.tsv"), "w", encoding="utf-8") as f:
        f.write("rank\tterm\tfreq\n")
        for r, (term, fr) in enumerate(terms, 1):
            f.write(f"{r}\t{term}\t{fr}\n")

    with open(os.path.join(out, "zipf.dat"), "w", encoding="utf-8") as f:
        f.write("rank freq zipf\n")
        for r in plot_ranks(len(terms)):
            f.write(f"{r} {terms[r - 1][1]} {c / r:.4f}\n")

    summary = [("files", len(paths)), ("tokens", tokens), ("terms", len(terms)), ("hapax", hapax),
               ("hapax_share", f"{hapax / len(terms):.4f}"), ("C", c)]
    with open(os.path.join(out, "summary.tsv"), "w", encoding="utf-8") as f:
        f.write("".join(f"{k}\t{v}\n" for k, v in summary))
    print("".join(f"{k}\t{v}\n" for k, v in summary), end="")

    with open(os.path.join(out, "ranks-table.tex"), "w", encoding="utf-8") as f:
        f.write("\\begin{tabular}{|r|l|r|r|r|r|}\n\\hline\n")
        f.write("$r$ & Термин & $f$ & $f \\cdot r$ & $C/r$ & $f / (C/r)$ \\\\\n\\hline\n")
        n = len(terms)
        for r in sorted({r for r in TABLE_RANKS if r < n} | {n} | ({n - hapax + 1} if hapax else set())):
            term, fr = terms[r - 1]
            f.write(f"{num(r)} & \\texttt{{{tex_term(term)}}} & {num(fr)} & {num(fr * r)} & "
                    f"{num(c / r, 1)} & {num(fr / (c / r), 2)} \\\\\n\\hline\n")
        f.write("\\end{tabular}\n")


if __name__ == "__main__":
    main()
