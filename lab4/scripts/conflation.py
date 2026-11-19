#!/usr/bin/env python3
import os
import subprocess

from common import ROOT

STEM = os.path.join(ROOT, "build", "stem")
FREQ = os.path.join(ROOT, "data", "zipf", "freq.tsv")
SHOW = ["hostel", "ski", "castl", "volcano", "new", "gener", "hospit", "organ", "univers", "nation"]
FORMS = 6


def main():
    terms = []
    with open(FREQ, encoding="utf-8") as f:
        next(f)
        for line in f:
            _, term, freq = line.rstrip("\n").split("\t")
            terms.append((term, int(freq)))
    out = subprocess.run([STEM], input="".join(t + "\n" for t, _ in terms), capture_output=True, text=True,
                         check=True).stdout.splitlines()
    groups = {}
    for (term, freq), line in zip(terms, out):
        token, stem = line.split("\t")
        assert token == term
        groups.setdefault(stem, []).append((freq, term))
    multi = [g for g in groups.values() if len(g) > 1]
    lines = [f"terms\t{len(terms)}", f"stems\t{len(groups)}", f"stems_with_2plus_forms\t{len(multi)}",
             f"terms_in_such_stems\t{sum(len(g) for g in multi)}", ""]
    rows = []
    for stem in SHOW:
        forms = sorted(groups.get(stem, []), reverse=True)
        lines.append(f"{stem}\t{len(forms)}\t" + ", ".join(f"{t} {fr}" for fr, t in forms[:20]))
        shown = ", ".join(f"{t}~({fr:,})".replace(",", "\\,") for fr, t in forms[:FORMS])
        rows.append(f"\\texttt{{{stem}}} & {len(forms)} & {shown}{', \\ldots' if len(forms) > FORMS else ''} \\\\\n\\hline\n")
    with open(os.path.join(ROOT, "results", "conflation.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    with open(os.path.join(ROOT, "report", "src", "conflation-table.tex"), "w", encoding="utf-8") as f:
        f.write("\\begin{tabular}{|l|r|p{0.68\\textwidth}|}\n\\hline\nОснова & Форм & Самые частые формы (число вхождений) \\\\\n\\hline\n"
                + "".join(rows) + "\\end{tabular}\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
