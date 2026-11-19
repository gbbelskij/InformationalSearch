#!/usr/bin/env python3
import hashlib
import os
import re
import statistics
import struct
import subprocess
import sys

from common import (LISTBENCH, MIB, QUERIES, RAW_INDEX, RESULTS, SEARCH, VB_INDEX, kv, loadavg, num, table, tex)

GROUPS = [("all", "Все термы"), ("rare", "Редкие, $df \\le 100$"), ("medium", "Средние, $100 < df \\le 10^4$"),
          ("high", "Частые, $df > 10^4$")]
BINS = [("df1", "1"), ("df2-10", "2--10"), ("df11-100", "11--100"), ("df101-1000", "101--$10^3$"),
        ("df1001-10000", "$10^3$--$10^4$"), ("df10001-100000", "$10^4$--$10^5$"), ("df100001+", "$> 10^5$")]
ROUNDS = 2
LIST_REPEAT = 5
SEARCH_REPEAT = 20


def listbench(path):
    res = subprocess.run([LISTBENCH, path, "-r", str(LIST_REPEAT)], capture_output=True, text=True, check=True)
    head, groups = {}, {}
    for line in res.stdout.splitlines():
        p = line.split("\t")
        if p[0] == "group":
            groups[p[1]] = {"terms": int(p[4]), "postings": int(p[5]), "bytes": int(p[6]), "ms": float(p[7]),
                            "sum": int(p[8])}
        else:
            head[p[0]] = p[1]
    return head, groups


def best_lists(path, runs):
    head, groups = runs[0]
    for h, g in runs[1:]:
        head["load_ms"] = str(min(float(head["load_ms"]), float(h["load_ms"])))
        for k in groups:
            groups[k]["ms"] = min(groups[k]["ms"], g[k]["ms"])
    return head, groups


def blocks(text):
    out = []
    for chunk in text.split("\n\n"):
        lines = chunk.strip("\n").split("\n")
        if lines and lines[0].startswith("query:"):
            out.append(dict(line.split(": ", 1) if ": " in line else (line.rstrip(":"), "") for line in lines[:4]))
    return out


def timed(index, queries):
    res = subprocess.run([SEARCH, index, "-n", "1", "-r", str(SEARCH_REPEAT)], input="".join(q + "\n" for q in queries),
                         capture_output=True, text=True, check=True)
    load = float(re.search(r"([\d.]+) ms", res.stderr).group(1))
    return load, blocks(res.stdout)


def full_hash(index, queries):
    p = subprocess.Popen([SEARCH, index, "-n", "0"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL)
    p.stdin.write("".join(q + "\n" for q in queries).encode())
    p.stdin.close()
    h, lines = hashlib.sha256(), 0
    for line in p.stdout:
        if not line.startswith(b"time_us:"):
            h.update(line)
            lines += 1
    p.wait()
    return h.hexdigest(), lines


def parts(path):
    with open(path, "rb") as f:
        b = f.read(64)
    n, t = struct.unpack_from("<II", b, 12)
    docs_off, terms_off, postings_off, strings_off = struct.unpack_from("<QQQQ", b, 32)
    size = os.path.getsize(path)
    return [64, terms_off - docs_off, postings_off - terms_off, strings_off - postings_off, size - strings_off, size]


def main():
    global RAW_INDEX, VB_INDEX
    if len(sys.argv) == 3:
        RAW_INDEX, VB_INDEX = sys.argv[1], sys.argv[2]
    load = loadavg()
    with open(QUERIES, encoding="utf-8") as f:
        queries = [line.rstrip("\n") for line in f if line.strip()]

    raw_runs, vb_runs = [], []
    for _ in range(ROUNDS):
        raw_runs.append(listbench(RAW_INDEX))
        vb_runs.append(listbench(VB_INDEX))
    rh, rg = best_lists(RAW_INDEX, raw_runs)
    vh, vg = best_lists(VB_INDEX, vb_runs)

    with open(os.path.join(RESULTS, "lists.tsv"), "w", encoding="utf-8") as f:
        f.write(f"# loadavg_before {load}; traversal = read_list into array + sum, min of {ROUNDS}x{LIST_REPEAT}\n")
        f.write("group\tterms\tpostings\traw_bytes\tvb_bytes\tbits_per_posting\traw_ms\tvb_ms\traw_ns\tvb_ns\n")
        for k, _ in GROUPS + BINS:
            r, v = rg[k], vg[k]
            p = max(r["postings"], 1)
            f.write(f"{k}\t{r['terms']}\t{r['postings']}\t{r['bytes']}\t{v['bytes']}\t{8 * v['bytes'] / p:.3f}\t"
                    f"{r['ms']:.3f}\t{v['ms']:.3f}\t{1e6 * r['ms'] / p:.3f}\t{1e6 * v['ms'] / p:.3f}\n")

    def group_row(label, k):
        r, v = rg[k], vg[k]
        p = r["postings"]
        return [label, num(r["terms"]), num(p), num(r["bytes"] / MIB, 1), num(v["bytes"] / MIB, 1),
                num(8 * v["bytes"] / p, 2), num(r["ms"], 1), num(v["ms"], 1), num(1e6 * r["ms"] / p, 2),
                num(1e6 * v["ms"] / p, 2)]

    head = ["Группа", "Термов", "Записей", "\\multicolumn{2}{c|}{МиБ}", "Бит", "\\multicolumn{2}{c|}{мс}",
            "\\multicolumn{2}{c|}{нс/зап.}"]
    sub = ["", "", "", "u32", "VB", "/зап.", "u32", "VB", "u32", "VB"]
    rows = [sub] + [group_row(label, k) for k, label in GROUPS]
    table("lists-table.tex", head, rows, "p{0.25\\textwidth}|r|r|r|r|r|r|r|r|r|")
    table("bins-table.tex", ["$df$"] + head[1:], [sub] + [group_row(label, k) for k, label in BINS if rg[k]["postings"]],
          "l|r|r|r|r|r|r|r|r|r|")

    pr, pv = parts(RAW_INDEX), parts(VB_INDEX)
    names = ["Заголовок", "Таблица документов", "Таблица термов", "Списки документов", "Пул строк", "Всего"]
    table("parts-table.tex", ["Раздел", "Без сжатия, байт", "VByte, байт", "VByte / без сжатия, \\%"],
          [[n, num(a), num(b), num(100 * b / a, 1)] for n, a, b in zip(names, pr, pv)])

    raw_loads, vb_loads, raw_t, vb_t = [], [], [], []
    for _ in range(3):
        a, ba = timed(RAW_INDEX, queries)
        b, bb = timed(VB_INDEX, queries)
        raw_loads.append(a)
        vb_loads.append(b)
        raw_t.append([float(x["time_us"]) for x in ba])
        vb_t.append([float(x["time_us"]) for x in bb])
        info, info_vb = ba, bb
    raw_best = [min(r[i] for r in raw_t) for i in range(len(queries))]
    vb_best = [min(r[i] for r in vb_t) for i in range(len(queries))]
    with open(os.path.join(RESULTS, "search.tsv"), "w", encoding="utf-8") as f:
        f.write(f"# loadavg_before {load}; repeat {SEARCH_REPEAT}, min of 3 runs\n")
        f.write("query\tparsed\tfound\traw_us\tvb_us\n")
        for q, x, a, b in zip(queries, info, raw_best, vb_best):
            f.write(f"{q}\t{x['parsed']}\t{x['found']}\t{a:.1f}\t{b:.1f}\n")
    table("search-table.tex", ["\\textnumero", "Запрос после разбора", "Найдено", "u32, мкс", "VB, мкс"],
          [[str(i), f"\\texttt{{{tex(x['parsed'])}}}", num(int(x["found"])), num(a, 1), num(b, 1)]
           for i, (x, a, b) in enumerate(zip(info, raw_best, vb_best), 1)],
          "r|p{0.5\\textwidth}|r|r|r|", long=True)
    summary = {
        "queries": len(queries),
        "raw_load_ms": min(raw_loads), "vb_load_ms": min(vb_loads),
        "raw_median_us": statistics.median(raw_best), "vb_median_us": statistics.median(vb_best),
        "raw_mean_us": statistics.mean(raw_best), "vb_mean_us": statistics.mean(vb_best),
        "raw_max_us": max(raw_best), "vb_max_us": max(vb_best),
        "raw_list_load_ms": float(rh["load_ms"]), "vb_list_load_ms": float(vh["load_ms"]),
        "raw_all_ns": 1e6 * rg["all"]["ms"] / rg["all"]["postings"],
        "vb_all_ns": 1e6 * vg["all"]["ms"] / vg["all"]["postings"],
        "loadavg_before": load,
    }
    table("search-summary-table.tex", ["Показатель", "Без сжатия", "VByte"], [
        ["Загрузка индекса в память, мс", num(summary["raw_load_ms"]), num(summary["vb_load_ms"])],
        ["Запрос: медиана, мкс", num(summary["raw_median_us"], 1), num(summary["vb_median_us"], 1)],
        ["Запрос: среднее, мкс", num(summary["raw_mean_us"], 1), num(summary["vb_mean_us"], 1)],
        ["Запрос: максимум, мкс", num(summary["raw_max_us"], 1), num(summary["vb_max_us"], 1)],
    ], "p{0.45\\textwidth}|r|r|")

    lines, ok = [], True

    def check(cond, text):
        nonlocal ok
        lines.append(f"[{'ok' if cond else 'FAIL'}] {text}")
        ok = ok and cond

    v = subprocess.run([LISTBENCH, "-v", RAW_INDEX, VB_INDEX], capture_output=True, text=True)
    vk = kv(v.stdout)
    check(v.returncode == 0 and vk.get("term_mismatches") == "0" and vk.get("doc_mismatches") == "0",
          f"listbench -v: {vk.get('terms_checked')} термов, {vk.get('postings_checked')} записей, "
          f"{vk.get('docs_checked')} документов — расхождений {vk.get('term_mismatches')} и {vk.get('doc_mismatches')}")
    check(all(rg[k]["sum"] == vg[k]["sum"] and rg[k]["postings"] == vg[k]["postings"] for k, _ in GROUPS + BINS),
          "listbench: суммы docID по каждой группе термов одинаковы для обоих индексов")
    check([x["found"] for x in info] == [x["found"] for x in info_vb] and
          [x["parsed"] for x in info] == [x["parsed"] for x in info_vb],
          f"search: число найденных совпадает во всех {len(queries)} запросах")
    ha, la = full_hash(RAW_INDEX, queries)
    hb, lb = full_hash(VB_INDEX, queries)
    check(ha == hb and la == lb, f"search -n 0: полная выдача {la} строк, SHA-256 совпадает ({ha[:16]}…)")
    lines.append(f"Итого: {'все проверки пройдены' if ok else 'есть ошибки'}")
    with open(os.path.join(RESULTS, "verify.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    with open(os.path.join(RESULTS, "bench-summary.tsv"), "w", encoding="utf-8") as f:
        for k, x in summary.items():
            f.write(f"{k}\t{x:.1f}\n" if isinstance(x, float) else f"{k}\t{x}\n")
    print(open(os.path.join(RESULTS, "lists.tsv"), encoding="utf-8").read())
    print(open(os.path.join(RESULTS, "bench-summary.tsv"), encoding="utf-8").read())
    print("\n".join(lines))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
