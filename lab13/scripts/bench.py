#!/usr/bin/env python3
import os
import re
import statistics
import struct

from common import PLAIN_INDEX, RESULTS, ROOT, SYSTEMS, TF_INDEX, num, read_queries, run, table, tex

VB_INDEX = os.path.normpath(os.path.join(ROOT, "..", "lab11", "data", "index-vb.bin"))
LAB8_QUERIES = os.path.normpath(os.path.join(ROOT, "..", "lab8", "data", "queries.txt"))
REPEAT = 10
MIB = 2 ** 20


def parts(path):
    with open(path, "rb") as f:
        b = f.read(64)
    docs_off, terms_off, postings_off, strings_off = struct.unpack_from("<QQQQ", b, 32)
    size = os.path.getsize(path)
    return [terms_off - docs_off, strings_off - postings_off, size]


def timed(index, queries):
    best, load = None, None
    for _ in range(3):
        out, err = run(index, queries, "-n", "1", "-r", str(REPEAT))
        t = [float(b["time_us"]) for b in out]
        best = t if best is None else [min(a, b) for a, b in zip(best, t)]
        ms = float(re.search(r"([\d.]+) ms", err).group(1))
        load = ms if load is None else min(load, ms)
    return best, load, out


def main():
    with open("/proc/loadavg") as f:
        loadavg = f.read().split()[0]
    sizes = {name: parts(p) for name, p in (("plain", PLAIN_INDEX), ("vb", VB_INDEX), ("tf", TF_INDEX))}
    table("sizes-table.tex", ["Индекс", "Таблица документов", "Списки документов", "Весь файл"],
          [[label] + [num(x / MIB, 1) for x in sizes[name]] for name, label in
           (("plain", "ЛР7: без частот, без сжатия"), ("vb", "ЛР11: без частот, VByte"),
            ("tf", "ЛР13: частоты, VByte"))], "l|r|r|r|")

    queries = read_queries()
    texts = [q for _, q, _ in queries]
    rows, tsv = [], ["qid\tquery\t" + "\t".join(f"{s[0]}_found\t{s[0]}_us" for s in SYSTEMS)]
    times, loads = {}, {}
    for name, _, index, form in SYSTEMS:
        t, load, out = timed(index, [form(q) for q in texts])
        times[name] = (t, [int(b["found"]) for b in out])
        loads[index] = load
    for i, (qid, q, _) in enumerate(queries):
        tsv.append(f"{qid}\t{q}\t" + "\t".join(f"{times[s[0]][1][i]}\t{times[s[0]][0][i]:.1f}" for s in SYSTEMS))
        rows.append([qid, f"\\texttt{{{tex(q)}}}"] +
                    [x for s in SYSTEMS for x in (num(times[s[0]][1][i]), num(times[s[0]][0][i] / 1000, 2))])
    table("speed-table.tex", ["\\textnumero", "Запрос"] +
          [f"\\multicolumn{{2}}{{c|}}{{{label}}}" for _, label, _, _ in SYSTEMS],
          [["", "", "найдено", "мс", "найдено", "мс", "найдено", "мс"]] + rows, "r|p{0.25\\textwidth}|r|r|r|r|r|r|")

    with open(LAB8_QUERIES, encoding="utf-8") as f:
        lab8 = [line.rstrip("\n") for line in f if line.strip()]
    plain_t, _, _ = timed(PLAIN_INDEX, lab8)
    tf_t, _, _ = timed(TF_INDEX, lab8)
    summary = {
        "loadavg_before": loadavg,
        "load_plain_ms": loads[PLAIN_INDEX], "load_tf_ms": loads[TF_INDEX],
        "eval_bool_median_us": statistics.median(times["bool"][0]),
        "eval_fuzzy_median_us": statistics.median(times["fuzzy"][0]),
        "eval_and_median_us": statistics.median(times["and"][0]),
        "eval_bool_mean_us": statistics.mean(times["bool"][0]),
        "eval_fuzzy_mean_us": statistics.mean(times["fuzzy"][0]),
        "eval_and_mean_us": statistics.mean(times["and"][0]),
        "lab8_plain_median_us": statistics.median(plain_t), "lab8_tf_median_us": statistics.median(tf_t),
        "lab8_plain_mean_us": statistics.mean(plain_t), "lab8_tf_mean_us": statistics.mean(tf_t),
        "lab8_plain_max_us": max(plain_t), "lab8_tf_max_us": max(tf_t),
    }
    with open(os.path.join(RESULTS, "bench.tsv"), "w", encoding="utf-8") as f:
        f.write(f"# loadavg_before {loadavg}; repeat {REPEAT}, min of 3 runs\n" + "\n".join(tsv) + "\n")
    with open(os.path.join(RESULTS, "lab8-queries.tsv"), "w", encoding="utf-8") as f:
        f.write("query\tplain_us\ttf_us\n" + "".join(f"{q}\t{a:.1f}\t{b:.1f}\n" for q, a, b in zip(lab8, plain_t, tf_t)))
    with open(os.path.join(RESULTS, "bench-summary.tsv"), "w", encoding="utf-8") as f:
        f.write("".join(f"{k}\t{v:.1f}\n" if isinstance(v, float) else f"{k}\t{v}\n" for k, v in summary.items()))
    table("bench-summary-table.tex", ["Показатель", "Булев (ЛР8)", "TF-IDF (ЛР13)"], [
        ["Загрузка индекса, мс", num(summary["load_plain_ms"]), num(summary["load_tf_ms"])],
        ["24 запроса ЛР8: медиана, мс", num(summary["lab8_plain_median_us"] / 1000, 2),
         num(summary["lab8_tf_median_us"] / 1000, 2)],
        ["24 запроса ЛР8: среднее, мс", num(summary["lab8_plain_mean_us"] / 1000, 2),
         num(summary["lab8_tf_mean_us"] / 1000, 2)],
        ["24 запроса ЛР8: максимум, мс", num(summary["lab8_plain_max_us"] / 1000, 1),
         num(summary["lab8_tf_max_us"] / 1000, 1)],
    ], "p{0.45\\textwidth}|r|r|")
    print(open(os.path.join(RESULTS, "bench-summary.tsv"), encoding="utf-8").read())


if __name__ == "__main__":
    main()
