import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SEARCH = os.path.join(ROOT, "..", "lab8", "build", "search")
DOCS = os.path.join(ROOT, "data", "docs.tsv")
QUERIES = os.path.join(ROOT, "data", "queries.tsv")
QRELS = os.path.join(ROOT, "data", "qrels.tsv")
SYSTEMS = [("plain", os.path.join(ROOT, "data", "index.bin")),
           ("stem", os.path.join(ROOT, "data", "index-stem.bin"))]
K = 10


def load_queries():
    with open(QUERIES, encoding="utf-8") as f:
        return [line.rstrip("\n").split("\t") for line in f if line.strip()]


def search(index, queries, limit=K):
    res = subprocess.run([SEARCH, index, "-n", str(limit)], input="".join(q + "\n" for q in queries),
                         capture_output=True, text=True, check=True)
    out = []
    for chunk in res.stdout.split("\n\n"):
        lines = chunk.strip("\n").split("\n")
        if not lines or not lines[0].startswith("query:"):
            continue
        head = dict(line.split(": ", 1) if ": " in line else (line.rstrip(":"), "") for line in lines[:4])
        head["docs"] = [int(line.split("\t")[1]) for line in lines[4:]]
        out.append(head)
    return out


def run_all(queries, limit=K):
    return {name: search(index, [q[1] for q in queries], limit) for name, index in SYSTEMS}
