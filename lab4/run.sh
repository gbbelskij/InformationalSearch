#!/bin/sh
set -e
cd "$(dirname "$0")"
mkdir -p data results
make
make -C ../lab3
make -C ../lab7
make -C ../lab8
if [ ! -f data/index.bin ]; then
    python3 ../lab7/scripts/doclist.py ../lab1/data/raw ../lab1/data/text > data/docs.tsv
    ../lab7/build/indexer data/index.bin < data/docs.tsv
fi
if [ ! -f data/zipf/freq.tsv ]; then
    python3 ../lab5/scripts/zipf.py ../lab1/data/text data/zipf
fi
make -s test | tee results/tests.log
python3 scripts/index_stats.py
python3 scripts/conflation.py
python3 scripts/pool.py
python3 scripts/evaluate.py
cd report && pdflatex -interaction=nonstopmode report.tex >/dev/null && pdflatex -interaction=nonstopmode report.tex >/dev/null
