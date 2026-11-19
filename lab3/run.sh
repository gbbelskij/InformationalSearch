#!/bin/sh
set -e
cd "$(dirname "$0")"
mkdir -p results
make
make -s test | tee results/tests.log
python3 scripts/bench.py "$@"
cd report && pdflatex -interaction=nonstopmode report.tex >/dev/null && pdflatex -interaction=nonstopmode report.tex >/dev/null
