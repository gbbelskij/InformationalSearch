#!/bin/sh
set -e
cd "$(dirname "$0")"
mkdir -p data results
make
make -s test | tee results/tests.log
python3 scripts/doclist.py "${1:-../lab1/data_1m/raw}" "${2:-../lab1/data_1m/text}" > data/docs.tsv
python3 scripts/stats.py
cd report && pdflatex -interaction=nonstopmode report.tex >/dev/null && pdflatex -interaction=nonstopmode report.tex >/dev/null
