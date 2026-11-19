#!/bin/sh
set -e
cd "$(dirname "$0")"
mkdir -p results
make -C ../lab7
if [ ! -f ../lab7/data/index.bin ]; then
    mkdir -p ../lab7/data
    python3 ../lab7/scripts/doclist.py ../lab1/data_1m/raw ../lab1/data_1m/text > ../lab7/data/docs.tsv
    ../lab7/build/indexer ../lab7/data/index.bin < ../lab7/data/docs.tsv
fi
make
make -s test | tee results/tests.log
python3 tests/test_web.py | tee results/tests-web.log
python3 scripts/bench.py
cd report && pdflatex -interaction=nonstopmode report.tex >/dev/null && pdflatex -interaction=nonstopmode report.tex >/dev/null
