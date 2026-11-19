#!/usr/bin/env python3
import os
import sys

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from crawler import connect  # noqa: E402


def main():
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--tex"):
        sys.exit("usage: stats.py <config.yaml> [--tex]")
    with open(sys.argv[1], encoding="utf-8") as f:
        cfg = yaml.safe_load(f)
    client, db = connect(cfg["db"])
    head = ["Источник", "Док.", "Без сохр.", "Дубл.", "Ошибок", "В очереди", "HTML, МиБ", "Ср. док., КиБ", "Глубина"]
    rows = []
    for src in sorted(db.frontier.distinct("source"), reverse=True):
        agg = list(db.documents.aggregate([
            {"$match": {"source": src}},
            {"$lookup": {"from": "frontier", "localField": "_id", "foreignField": "_id", "as": "f"}},
            {"$group": {"_id": None, "docs": {"$sum": 1}, "size": {"$sum": {"$strLenBytes": "$html"}},
                        "depth": {"$max": {"$first": "$f.depth"}}}}]))
        docs, size, depth = (agg[0]["docs"], agg[0]["size"], agg[0]["depth"]) if agg else (0, 0, 0)
        status = {r["_id"]: r["n"] for r in db.frontier.aggregate([
            {"$match": {"source": src}}, {"$group": {"_id": "$status", "n": {"$sum": 1}}}])}
        service = status.get("done", 0) - docs
        rows.append([src, docs, service, status.get("dup", 0), status.get("error", 0), status.get("new", 0),
                     size / 2**20, size / docs / 2**10 if docs else 0.0, depth])
    client.close()
    total = [sum(r[i] for r in rows) for i in range(1, 7)]
    rows.append(["Итого"] + total + [total[5] / total[0] * 2**10 if total[0] else 0.0, max(r[8] for r in rows)])

    print("\t".join(head))
    for r in rows:
        print("\t".join(f"{v:.1f}" if isinstance(v, float) else str(v) for v in r))
    if len(sys.argv) == 3:
        with open(os.path.join(HERE, "..", "report", "src", "stats-table.tex"), "w", encoding="utf-8") as f:
            f.write("\\begin{tabular}{|l|" + "r|" * (len(head) - 1) + "}\n\\hline\n")
            f.write(" & ".join(head) + " \\\\\n\\hline\n")
            for r in rows:
                f.write(" & ".join(f"{v:.1f}".replace(".", ",") if isinstance(v, float) else str(v).capitalize()
                                   for v in r) + " \\\\\n\\hline\n")
            f.write("\\end{tabular}\n")


if __name__ == "__main__":
    main()
