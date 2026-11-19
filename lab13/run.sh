#!/bin/sh
set -e
cd "$(dirname "$0")"
mkdir -p data results
make -C ../lab7
make -C ../lab8
make
make -s test | tee results/tests.log
if [ ! -f ../lab7/data/docs.tsv ]; then
    mkdir -p ../lab7/data
    python3 ../lab7/scripts/doclist.py ../lab1/data_1m/raw ../lab1/data_1m/text > ../lab7/data/docs.tsv
fi
if [ ! -f ../lab7/data/index.bin ]; then
    ../lab7/build/indexer ../lab7/data/index.bin < ../lab7/data/docs.tsv > /dev/null
fi
if [ ! -f data/index-tf.bin ]; then
    ../lab7/build/indexer -c -t data/index-tf.bin < ../lab7/data/docs.tsv > data/indexing.tsv
fi
python3 scripts/evaluate.py
python3 scripts/bench.py
cd report && pdflatex -interaction=nonstopmode report.tex >/dev/null && pdflatex -interaction=nonstopmode report.tex >/dev/null
