#!/usr/bin/env python3
"""Статистика корпуса: размеры сырых данных и выделенного текста по источникам.

stats.py [DATA_DIR [TABLE]] — по умолчанию data и report/src/stats-table.tex.
Печатает TSV в stdout и пишет таблицу для отчёта.
"""
import json
import os
import statistics
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ORDER = ["wikivoyage", "wikipedia", "travelse", "reddit"]
NAMES = {"wikivoyage": "Wikivoyage", "wikipedia": "Wikipedia", "travelse": "Travel SE", "reddit": "Reddit"}


def files(folder, skip=("meta.jsonl",)):
    out = {}
    with os.scandir(folder) as it:
        for e in it:
            if e.is_file() and e.name not in skip:
                out[os.path.splitext(e.name)[0]] = e.stat().st_size
    return out


def word_counts(data, src, ids):
    counts = {}
    meta = os.path.join(data, "raw", src, "meta.jsonl")
    if os.path.exists(meta):
        with open(meta, encoding="utf-8") as f:
            for line in f:
                m = json.loads(line)
                if "words" in m:
                    counts[str(m["id"])] = m["words"]
    for i in ids:
        if i not in counts:
            with open(os.path.join(data, "text", src, i + ".txt"), encoding="utf-8") as f:
                counts[i] = len(f.read().split())
    return [counts[i] for i in ids]


def kb(b):
    return f"{b / 1024:.1f}".replace(".", ",")


def mb(b):
    return f"{b / 1024 / 1024:,.1f}".replace(",", "\\,").replace(".", ",")


def num(v):
    return f"{v:,}".replace(",", "\\,")


def main():
    data = os.path.join(ROOT, sys.argv[1] if len(sys.argv) > 1 else "data")
    table = sys.argv[2] if len(sys.argv) > 2 else "stats-table.tex"
    found = sorted(os.listdir(os.path.join(data, "text")))
    sources = [s for s in ORDER if s in found] + [s for s in found if s not in ORDER]
    rows, all_words = [], []
    for src in sources:
        raw = files(os.path.join(data, "raw", src))
        txt = files(os.path.join(data, "text", src))
        ids = sorted(txt)
        words = word_counts(data, src, ids)
        all_words += words
        rows.append((NAMES.get(src, src), len(ids), sum(raw.values()), sum(txt.values()), words))
    rows.append(("Итого", sum(r[1] for r in rows), sum(r[2] for r in rows), sum(r[3] for r in rows), all_words))

    print("source\tdocs\traw_bytes\ttext_bytes\twords\tavg_raw_bytes\tavg_text_bytes\tavg_words\tmedian_words")
    for name, n, raw, txt, w in rows:
        print(f"{name}\t{n}\t{raw}\t{txt}\t{sum(w)}\t{raw // n}\t{txt // n}\t{sum(w) // n}\t{statistics.median(w):.0f}")

    with open(os.path.join(ROOT, "report", "src", table), "w", encoding="utf-8") as f:
        f.write("\\begin{tabular}{|l|r|r|r|r|r|r|r|}\n\\hline\n"
                "Источник & Док. & Сырые, МиБ & Текст, МиБ & Ср. док., КиБ"
                " & Ср. текст, КиБ & Ср. слов & Медиана слов \\\\\n\\hline\n")
        for name, n, raw, txt, w in rows:
            f.write(f"{name} & {num(n)} & {mb(raw)} & {mb(txt)} & {kb(raw / n)}"
                    f" & {kb(txt / n)} & {num(sum(w) // n)} & {num(int(statistics.median(w)))} \\\\\n\\hline\n")
        f.write("\\end{tabular}\n")


if __name__ == "__main__":
    main()
