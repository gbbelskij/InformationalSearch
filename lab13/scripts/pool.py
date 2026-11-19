#!/usr/bin/env python3
import os
import random
import re

from common import RESULTS, ROOT, SYSTEMS, doc_paths, doc_text, read_queries, top

HEAD = 900
WINDOW = 160
PARTS = 3


def excerpt(text, words):
    text = re.sub(r"\s+", " ", text).strip()
    out = [text[:HEAD] + ("…" if len(text) > HEAD else "")]
    low = text.lower()
    for w in words:
        for m in re.finditer(r"\b" + re.escape(w), low[HEAD:]):
            p = HEAD + m.start()
            out.append("…" + text[max(HEAD, p - WINDOW):p + WINDOW] + "…")
            break
    return "\n".join(out)


def main():
    queries = read_queries()
    texts = [q for _, q, _ in queries]
    runs = {s[0]: top(s, texts) for s in SYSTEMS}
    paths = doc_paths()
    rnd = random.Random(13)
    key_lines = ["qid\tkey\tdoc"]
    parts = [[] for _ in range(PARTS)]
    for i, (qid, q, need) in enumerate(queries):
        docs = {}
        for name, _, _, _ in SYSTEMS:
            for hit in runs[name][i]["hits"]:
                docs[int(hit[1])] = hit[2]
        order = sorted(docs)
        rnd.shuffle(order)
        lines = [f"=== Запрос {qid}: {q}", f"Потребность: {need}", f"Документов: {len(order)}", ""]
        for n, doc in enumerate(order, 1):
            key = f"{qid}-{n:02d}"
            key_lines.append(f"{qid}\t{key}\t{doc}")
            path = paths[doc]
            source = path.split("/")[-2]
            lines += [f"--- {key}", f"Заголовок: {docs[doc]}", f"Источник: {source}",
                      excerpt(doc_text(path), q.split()), ""]
        parts[i * PARTS // len(queries)].append("\n".join(lines))
    with open(os.path.join(ROOT, "data", "pool-key.tsv"), "w", encoding="utf-8") as f:
        f.write("\n".join(key_lines) + "\n")
    for n, part in enumerate(parts, 1):
        with open(os.path.join(RESULTS, f"pool-{n}.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(part))
    print(f"{len(key_lines) - 1} documents in pool")


if __name__ == "__main__":
    main()
