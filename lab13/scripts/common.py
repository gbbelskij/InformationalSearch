import os
import re
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
LAB7 = os.path.normpath(os.path.join(ROOT, "..", "lab7"))
SEARCH = os.path.normpath(os.path.join(ROOT, "..", "lab8", "build", "search"))
INDEXER = os.path.join(LAB7, "build", "indexer")
DOCS = os.path.join(LAB7, "data", "docs.tsv")
PLAIN_INDEX = os.path.join(LAB7, "data", "index.bin")
TF_INDEX = os.path.join(ROOT, "data", "index-tf.bin")
QUERIES = os.path.join(ROOT, "data", "queries.tsv")
QRELS = os.path.join(ROOT, "data", "qrels.tsv")
RESULTS = os.path.join(ROOT, "results")
TEX = os.path.join(ROOT, "report", "src")
DEPTH = 10

SYSTEMS = [
    ("bool", "Булев (ЛР8), по docID", PLAIN_INDEX, lambda q: q),
    ("fuzzy", "TF-IDF, нечёткий", TF_INDEX, lambda q: q),
    ("and", "TF-IDF, строгое И", TF_INDEX, lambda q: " && ".join(q.split())),
]


def read_queries():
    with open(QUERIES, encoding="utf-8") as f:
        next(f)
        return [line.rstrip("\n").split("\t") for line in f if line.strip()]


def blocks(text):
    out = []
    for chunk in text.split("\n\n"):
        lines = chunk.strip("\n").split("\n")
        if not lines or not lines[0].startswith("query:"):
            continue
        head = dict(line.split(": ", 1) if ": " in line else (line.rstrip(":"), "") for line in lines[:4])
        head["hits"] = [line.split("\t") for line in lines[4:]]
        out.append(head)
    return out


def run(index, queries, *args):
    res = subprocess.run([SEARCH, index, *args], input="".join(q + "\n" for q in queries), capture_output=True,
                         text=True, check=True)
    return blocks(res.stdout), res.stderr


def top(system, queries, depth=DEPTH):
    _, _, index, form = system
    out, _ = run(index, [form(q) for q in queries], "-n", str(depth))
    return out


def doc_paths():
    with open(DOCS, encoding="utf-8") as f:
        return [line.split("\t", 1)[0] for line in f]


def doc_text(path):
    with open(os.path.join(LAB7, path), encoding="utf-8") as f:
        return f.read()


def num(v, digits=0):
    return f"{v:,.{digits}f}".replace(",", "\\,").replace(".", ",")


def tex(s):
    s = (s.replace("\\", "\\textbackslash{}").replace("&", "\\&").replace("|", "\\textbar{}")
         .replace("_", "\\_").replace("%", "\\%").replace("#", "\\#").replace("$", "\\$")
         .replace("{", "\\{").replace("}", "\\}").replace("~", "\\textasciitilde{}").replace("^", "\\^{}"))
    return re.sub(r"[^\x00-\u024f\u0400-\u04ff\u2010-\u2027]", "?", s)


def table(name, head, rows, align=None, long=False):
    env = "longtable" if long else "tabular"
    with open(os.path.join(TEX, name), "w", encoding="utf-8") as f:
        f.write(f"\\begin{{{env}}}{{|" + (align or "l|" + "r|" * (len(head) - 1)) + "}\n\\hline\n")
        f.write(" & ".join(head) + " \\\\\n\\hline\n")
        if long:
            f.write("\\endhead\n")
        for r in rows:
            f.write(" & ".join(r) + " \\\\\n\\hline\n")
        f.write(f"\\end{{{env}}}\n")
