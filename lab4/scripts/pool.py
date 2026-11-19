#!/usr/bin/env python3
import os
import re

from common import DOCS, ROOT, load_queries, run_all

WIDTH = 220


def snippets(path, prefixes):
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    scored = []
    for line in lines:
        words = re.findall(r"\w+", line.lower())
        hits = {p for p in prefixes if any(w.startswith(p) for w in words)}
        if hits:
            pos = min(m.start() for p in hits for m in [re.search(r"\b" + re.escape(p), line.lower())] if m)
            start = max(0, pos - WIDTH // 3)
            scored.append((-len(hits), line[start:start + WIDTH]))
    scored.sort(key=lambda x: x[0])
    return [s for _, s in scored[:3]]


def main():
    with open(DOCS, encoding="utf-8") as f:
        docs = [line.rstrip("\n").split("\t") for line in f]
    queries = load_queries()
    res = run_all(queries)
    out = []
    for i, (qid, query, need) in enumerate(queries):
        plain, stem = res["plain"][i], res["stem"][i]
        prefixes = sorted(set(re.findall(r"\w+", plain["parsed"]) + re.findall(r"\w+", stem["parsed"])))
        out.append(f"##### {qid}. [ {query} ] — {need}")
        out.append(f"plain: {plain['parsed']} (found {plain['found']}); stem: {stem['parsed']} (found {stem['found']})")
        union = sorted(set(plain["docs"]) | set(stem["docs"]))
        for d in union:
            who = "+".join(name for name, r in (("plain", plain), ("stem", stem)) if d in r["docs"])
            path, title, url = docs[d]
            out.append(f"--- doc {d} [{who}] {title}")
            out.extend(f"    > {s}" for s in snippets(path, prefixes))
        out.append("")
    with open(os.path.join(ROOT, "results", "pool.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"{sum(1 for line in out if line.startswith('--- doc'))} документов в пуле")


if __name__ == "__main__":
    main()
