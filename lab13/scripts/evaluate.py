#!/usr/bin/env python3
import glob
import math
import os
import sys

from common import QRELS, RESULTS, ROOT, SYSTEMS, num, read_queries, table, tex, top

MAX_GRADE = 2


def merge_assessments():
    key = {}
    with open(os.path.join(ROOT, "data", "pool-key.tsv"), encoding="utf-8") as f:
        next(f)
        for line in f:
            qid, k, doc = line.rstrip("\n").split("\t")
            key[k] = (qid, int(doc))
    rows = []
    for path in sorted(glob.glob(os.path.join(ROOT, "data", "assess-*.tsv"))):
        with open(path, encoding="utf-8") as f:
            next(f)
            for line in f:
                if line.strip():
                    k, grade, reason = (line.rstrip("\n").split("\t") + [""])[:3]
                    qid, doc = key[k.strip()]
                    rows.append((int(qid), doc, int(grade), reason.strip()))
    judged = {(q, d) for q, d, _, _ in rows}
    lost = [k for k, (q, d) in key.items() if (int(q), d) not in judged]
    if lost:
        sys.exit(f"evaluate: no grade for {len(lost)} pool documents: {' '.join(sorted(lost)[:10])}")
    with open(QRELS, "w", encoding="utf-8") as f:
        f.write("qid\tdoc\tgrade\treason\n")
        for q, d, g, r in sorted(rows):
            f.write(f"{q}\t{d}\t{g}\t{r}\n")


def read_qrels():
    qrels = {}
    with open(QRELS, encoding="utf-8") as f:
        next(f)
        for line in f:
            q, d, g, r = line.rstrip("\n").split("\t")
            qrels[(int(q), int(d))] = (int(g), r)
    return qrels


def dcg(grades):
    return sum((2 ** g - 1) / math.log2(i + 2) for i, g in enumerate(grades))


def err(grades):
    p, s = 1.0, 0.0
    for i, g in enumerate(grades):
        r = (2 ** g - 1) / 2 ** MAX_GRADE
        s += p * r / (i + 1)
        p *= 1 - r
    return s


def metrics(grades, ideal):
    g10 = (grades + [0] * 10)[:10]
    idcg = dcg(sorted(ideal, reverse=True)[:10])
    return {"p10": sum(1 for g in g10 if g >= 1) / 10, "ndcg10": dcg(g10) / idcg if idcg else 0.0, "err10": err(g10)}


def main():
    if not os.path.exists(QRELS):
        merge_assessments()
    qrels = read_qrels()
    queries = read_queries()
    texts = [q for _, q, _ in queries]
    runs = {s[0]: top(s, texts) for s in SYSTEMS}
    per, missing = {}, []
    lines = []
    for i, (qid, q, need) in enumerate(queries):
        qi = int(qid)
        ideal = [g for (qq, _), (g, _) in qrels.items() if qq == qi]
        lines.append(f"=== {qid}. {q}\n{need}")
        for name, label, _, _ in SYSTEMS:
            block = runs[name][i]
            grades = []
            lines.append(f"--- {label}: parsed [{block['parsed']}], найдено {block['found']}")
            for hit in block["hits"]:
                key = (qi, int(hit[1]))
                if key not in qrels:
                    missing.append(f"{qid}:{hit[1]}")
                    continue
                g, reason = qrels[key]
                grades.append(g)
                score = f" {hit[4]}" if len(hit) > 4 else ""
                lines.append(f"{hit[0]:>3} [{g}] {hit[1]:>7}{score} {hit[2]} — {reason}")
            per[(qid, name)] = dict(metrics(grades, ideal), found=int(block["found"]))
        lines.append("")
    if missing:
        sys.exit(f"evaluate: unjudged documents in top-{10}: {' '.join(missing[:20])}")
    with open(os.path.join(RESULTS, "runs.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    names = [s[0] for s in SYSTEMS]
    with open(os.path.join(RESULTS, "eval.tsv"), "w", encoding="utf-8") as f:
        f.write("qid\tquery\t" + "\t".join(f"{n}_{m}" for n in names for m in ("found", "p10", "ndcg10", "err10")) + "\n")
        for qid, q, _ in queries:
            f.write(f"{qid}\t{q}\t" + "\t".join(
                f"{per[(qid, n)]['found']}\t{per[(qid, n)]['p10']:.2f}\t{per[(qid, n)]['ndcg10']:.3f}\t"
                f"{per[(qid, n)]['err10']:.3f}" for n in names) + "\n")
    mean = {(n, m): sum(per[(qid, n)][m] for qid, _, _ in queries) / len(queries)
            for n in names for m in ("p10", "ndcg10", "err10")}
    better = {n: sum(per[(qid, n)]["ndcg10"] > per[(qid, "bool")]["ndcg10"] for qid, _, _ in queries) for n in names}
    worse = {n: sum(per[(qid, n)]["ndcg10"] < per[(qid, "bool")]["ndcg10"] for qid, _, _ in queries) for n in names}
    with open(os.path.join(RESULTS, "eval-summary.tsv"), "w", encoding="utf-8") as f:
        f.write("system\tp10\tndcg10\terr10\tndcg_better_than_bool\tndcg_worse_than_bool\n")
        for n in names:
            f.write(f"{n}\t{mean[(n, 'p10')]:.3f}\t{mean[(n, 'ndcg10')]:.3f}\t{mean[(n, 'err10')]:.3f}\t"
                    f"{better[n]}\t{worse[n]}\n")
    table("eval-table.tex",
          ["\\textnumero", "Запрос"] + [f"\\multicolumn{{2}}{{c|}}{{{label}}}" for _, label, _, _ in SYSTEMS],
          [["", "", "P@10", "NDCG", "P@10", "NDCG", "P@10", "NDCG"]] +
          [[qid, f"\\texttt{{{tex(q)}}}"] + [x for n in names for x in (num(per[(qid, n)]["p10"], 1),
                                                                         num(per[(qid, n)]["ndcg10"], 2))]
           for qid, q, _ in queries],
          "r|p{0.27\\textwidth}|r|r|r|r|r|r|")
    table("eval-summary-table.tex", ["Система", "P@10", "NDCG@10", "ERR@10", "NDCG лучше / хуже булева"],
          [[label, num(mean[(n, "p10")], 3), num(mean[(n, "ndcg10")], 3), num(mean[(n, "err10")], 3),
            "---" if n == "bool" else f"{better[n]} / {worse[n]}"] for n, label, _, _ in SYSTEMS],
          "l|r|r|r|r|")
    print(open(os.path.join(RESULTS, "eval-summary.tsv"), encoding="utf-8").read())


if __name__ == "__main__":
    main()
