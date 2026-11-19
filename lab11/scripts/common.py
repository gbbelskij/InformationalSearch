import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
LAB7 = os.path.normpath(os.path.join(ROOT, "..", "lab7"))
LAB8 = os.path.normpath(os.path.join(ROOT, "..", "lab8"))
INDEXER = os.path.join(LAB7, "build", "indexer")
SEARCH = os.path.join(LAB8, "build", "search")
LISTBENCH = os.path.join(ROOT, "build", "listbench")
DOCS = os.path.join(LAB7, "data", "docs.tsv")
RAW_INDEX = os.path.join(LAB7, "data", "index.bin")
VB_INDEX = os.path.join(ROOT, "data", "index-vb.bin")
QUERIES = os.path.join(LAB8, "data", "queries.txt")
RESULTS = os.path.join(ROOT, "results")
TEX = os.path.join(ROOT, "report", "src")
MIB = 2 ** 20


def loadavg():
    with open("/proc/loadavg") as f:
        return f.read().split()[0]


def kv(text):
    out = {}
    for line in text.splitlines():
        k, _, v = line.partition("\t")
        if v:
            out[k] = v
    return out


def num(v, digits=0):
    return f"{v:,.{digits}f}".replace(",", "\\,").replace(".", ",")


def tex(s):
    return (s.replace("\\", "\\textbackslash{}").replace("&", "\\&").replace("|", "\\textbar{}")
            .replace("_", "\\_").replace("%", "\\%").replace("#", "\\#"))


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
