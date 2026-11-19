#!/usr/bin/env python3
import itertools
import os
import subprocess
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
INDEXER = os.path.join(ROOT, "build", "indexer")
DOCS = os.path.join(ROOT, "data", "docs.tsv")
INDEX = os.path.join(ROOT, "data", "index.bin")
SIZES = [125000, 250000, 500000, 1000000]
REPEAT = 3


def run(lines, index):
    res = subprocess.run([INDEXER, index], input="".join(lines), capture_output=True, text=True, check=True)
    return {k: float(v) for k, v in (line.split("\t") for line in res.stdout.splitlines())}


def best(lines, index):
    runs = []
    for _ in range(REPEAT):
        runs.append(run(lines, index))
        time.sleep(0.5)
    return min(runs, key=lambda r: r["time_s"])


def num(v, digits=0):
    return f"{v:,.{digits}f}".replace(",", "\\,").replace(".", ",")


def table(name, head, rows, align=None):
    with open(os.path.join(ROOT, "report", "src", name), "w", encoding="utf-8") as f:
        f.write("\\begin{tabular}{|" + (align or "l|" + "r|" * (len(head) - 1)) + "}\n\\hline\n")
        f.write(" & ".join(head) + " \\\\\n\\hline\n")
        for r in rows:
            f.write(" & ".join(r) + " \\\\\n\\hline\n")
        f.write("\\end{tabular}\n")


def main():
    with open("/proc/loadavg") as f:
        load = f.read().split()[0]
    with open(DOCS, encoding="utf-8") as f:
        lines = f.readlines()

    s = best(lines, INDEX)
    with open(os.path.join(ROOT, "results", "stats.tsv"), "w", encoding="utf-8") as f:
        f.write(f"loadavg_before\t{load}\n" + "".join(f"{k}\t{v:.0f}\n" if v.is_integer() else f"{k}\t{v}\n" for k, v in s.items()))
    mib = 2 ** 20
    table("stats-table.tex", ["Показатель", "Значение"], [
        ["Документов", num(s["docs"])],
        ["Объём текста, МиБ", num(s["bytes"] / mib, 1)],
        ["Токенов", num(s["tokens"])],
        ["Термов", num(s["terms"])],
        ["Записей в списках документов (пар терм--документ)", num(s["postings"])],
        ["Средняя длина токена, символов", num(s["avg_token_len"], 2)],
        ["Средняя длина терма, символов", num(s["avg_term_len"], 2)],
        ["Размер индекса, МиБ", num(s["index_bytes"] / mib, 1)],
        ["Время индексации, с", num(s["time_s"], 2)],
        ["\\quad чтение файлов", num(s["read_s"], 2)],
        ["\\quad токенизация и вставка в словарь", num(s["index_s"], 2)],
        ["\\quad сортировка словаря", num(s["sort_s"], 2)],
        ["\\quad запись индекса", num(s["write_s"], 2)],
        ["Время на документ, мс", num(s["ms_per_doc"], 3)],
        ["Время на килобайт текста, мкс", num(s["us_per_kb"], 1)],
        ["Скорость, МиБ/с", num(s["kb_per_s"] / 1024, 1)],
        ["Пиковая память, МиБ", num(s["peak_rss_mb"], 1)],
    ], "p{0.62\\textwidth}|r|")
    n, t, p = s["docs"], s["terms"], s["postings"]
    parts = [("Заголовок", 64), ("Таблица документов", 12 * n), ("Таблица термов", 16 * t),
             ("Списки документов", 4 * p), ("Пул строк", s["index_bytes"] - 64 - 12 * n - 16 * t - 4 * p)]
    table("parts-table.tex", ["Раздел", "Байт", "\\%"],
          [[name, num(size), num(100 * size / s["index_bytes"], 1)] for name, size in parts])

    groups = {}
    for x in lines:
        groups.setdefault(os.path.basename(os.path.dirname(x.split("\t", 1)[0])), []).append(x)
    mixed = [x for row in itertools.zip_longest(*groups.values()) for x in row if x is not None]
    rows, tsv = [], ["docs\tmib\tterms\tpostings\tindex_mib\ttime_s\tus_per_kb\tpeak_rss_mb"]
    with tempfile.TemporaryDirectory() as tmp:
        for size in [n for n in SIZES if n < len(lines)] + [len(lines)]:
            r = s if size == len(lines) else best(mixed[:size], os.path.join(tmp, "index.bin"))
            tsv.append(f"{size}\t{r['bytes'] / mib:.1f}\t{r['terms']:.0f}\t{r['postings']:.0f}\t"
                       f"{r['index_bytes'] / mib:.1f}\t{r['time_s']:.3f}\t{r['us_per_kb']:.1f}\t{r['peak_rss_mb']:.1f}")
            rows.append([num(size), num(r["bytes"] / mib, 1), num(r["terms"]), num(r["postings"]),
                         num(r["index_bytes"] / mib, 1), num(r["time_s"], 2), num(r["us_per_kb"], 1),
                         num(r["peak_rss_mb"], 1)])
    with open(os.path.join(ROOT, "results", "scale.tsv"), "w", encoding="utf-8") as f:
        f.write("\n".join(tsv) + "\n")
    table("scale-table.tex", ["Док.", "Текст, МиБ", "Термов", "Записей", "Индекс, МиБ", "Время, с", "мкс/КиБ",
                              "Пам., МиБ"], rows)
    print(f"loadavg before: {load}")
    print("".join(f"{k}\t{v:.0f}\n" if v.is_integer() else f"{k}\t{v}\n" for k, v in s.items()))
    print("\n".join(tsv))


if __name__ == "__main__":
    main()
