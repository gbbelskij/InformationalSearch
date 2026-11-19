#!/bin/sh
set -e
cd "$(dirname "$0")"
mkdir -p results
make -C ../lab3
python3 tests/run_tests.py | tee results/tests.log
python3 scripts/zipf.py "${1:-../lab1/data_1m/text}" results
cd report && pdflatex -interaction=nonstopmode report.tex >/dev/null && pdflatex -interaction=nonstopmode report.tex >/dev/null
