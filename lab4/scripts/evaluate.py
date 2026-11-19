#!/usr/bin/env python3
import os
import sys

from common import K, QRELS, ROOT, load_queries, run_all


def num(v, digits=0):
    return f"{v:,.{digits}f}".replace(",", "\\,").replace(".", ",")


def tex(s):
    return s.replace("&", "\\&").replace("|", "\\textbar{}").replace("_", "\\_")


def main():
    queries = load_queries()
    qrels = {}
    with open(QRELS, encoding="utf-8") as f:
        for line in f:
            if line.strip():
                qid, doc, rel, comment = line.rstrip("\n").split("\t", 3)
                qrels[(qid, int(doc))] = (int(rel), comment)
    res = run_all(queries, 3 * K)
    rows, missing = [], []
    for i, (qid, query, need) in enumerate(queries):
        row = {"qid": qid, "query": query}
        exact = res["plain"][i]["docs"][:K]
        exact += [d for d in res["stem"][i]["docs"] if d not in exact][:K - len(exact)]
        res.setdefault("exact", []).append({"found": res["stem"][i]["found"], "parsed": "", "docs": exact})
        for name in ("plain", "stem", "exact"):
            r = res[name][i]
            r["docs"] = r["docs"][:K]
            rels = []
            for d in r["docs"]:
                if (qid, d) not in qrels:
                    missing.append((qid, d))
                rels.append(qrels.get((qid, d), (0, ""))[0])
            row[name] = {"found": int(r["found"]), "p": sum(rels) / K, "parsed": r["parsed"], "docs": r["docs"]}
        rows.append(row)
    if missing:
        sys.exit(f"evaluate: no judgments for (query, doc): {missing}")

    with open(os.path.join(ROOT, "results", "eval.tsv"), "w", encoding="utf-8") as f:
        f.write("qid\tquery\tplain_found\tplain_p10\tstem_found\tstem_p10\texact_first_p10\n")
        for r in rows:
            f.write(f"{r['qid']}\t{r['query']}\t{r['plain']['found']}\t{r['plain']['p']:.1f}\t"
                    f"{r['stem']['found']}\t{r['stem']['p']:.1f}\t{r['exact']['p']:.1f}\n")

    mean = {s: sum(r[s]["p"] for r in rows) / len(rows) for s in ("plain", "stem", "exact")}
    est = {s: sum(r[s]["found"] * r[s]["p"] for r in rows) for s in ("plain", "stem")}
    better = [r for r in rows if r["stem"]["p"] > r["plain"]["p"]]
    worse = [r for r in rows if r["stem"]["p"] < r["plain"]["p"]]
    lines = []
    for r in rows:
        d = r["stem"]["p"] - r["plain"]["p"]
        sign = f"{d:+.1f}".replace(".", ",") if d else "0"
        lines.append(f"{r['qid']} & \\texttt{{{tex(r['query'])}}} & "
                     f"{num(r['plain']['found'])} & {num(r['stem']['found'])} & {num(r['plain']['p'], 1)} & "
                     f"{num(r['stem']['p'], 1)} & {sign} & {num(r['exact']['p'], 1)} \\\\\n\\hline\n")
    with open(os.path.join(ROOT, "report", "src", "eval-table.tex"), "w", encoding="utf-8") as f:
        f.write("\\begin{tabular}{|r|l|r|r|r|r|r|r|}\n\\hline\n"
                " & & \\multicolumn{2}{c|}{Найдено} & \\multicolumn{4}{c|}{P@10} \\\\\n\\cline{3-8}\n"
                "\\textnumero & Запрос & без & со & без & со & $\\Delta$ & точн. \\\\\n\\hline\n"
                + "".join(lines) +
                f"\\multicolumn{{4}}{{|r|}}{{Среднее}} &{num(mean['plain'], 2)} & {num(mean['stem'], 2)} & "
                f"{num(mean['stem'] - mean['plain'], 2)} & {num(mean['exact'], 2)} \\\\\n\\hline\n\\end{{tabular}}\n")

    out = [f"mean_p10_plain\t{mean['plain']:.3f}", f"mean_p10_stem\t{mean['stem']:.3f}",
           f"mean_p10_exact_first\t{mean['exact']:.3f}",
           f"better\t{len(better)}", f"worse\t{len(worse)}", f"same\t{len(rows) - len(better) - len(worse)}",
           f"est_relevant_plain\t{est['plain']:.0f}", f"est_relevant_stem\t{est['stem']:.0f}"]
    with open(os.path.join(ROOT, "results", "eval-summary.tsv"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print("\n".join(out))

    detail = []
    for r in rows:
        p, s = r["plain"], r["stem"]
        detail.append(f"##### {r['qid']}. [ {r['query']} ]  P@10: {p['p']:.1f} -> {s['p']:.1f}, "
                      f"found: {p['found']} -> {s['found']}")
        for name, docs, other in (("только без стемминга", p["docs"], s["docs"]),
                                  ("только со стеммингом", s["docs"], p["docs"])):
            for d in docs:
                if d not in other:
                    rel, comment = qrels[(r["qid"], d)]
                    detail.append(f"  {name}: doc {d} rel={rel} — {comment}")
        detail.append("")
    with open(os.path.join(ROOT, "results", "changes.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(detail))


if __name__ == "__main__":
    main()
