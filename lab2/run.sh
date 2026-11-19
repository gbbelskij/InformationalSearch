#!/bin/sh
set -e
cd "$(dirname "$0")"
mkdir -p results
if [ ! -x .venv/bin/python ]; then
    python3 -m venv .venv
    .venv/bin/pip install -q pymongo pyyaml
fi
docker compose up -d
.venv/bin/python scripts/mongo_init.py
.venv/bin/python tests/run_tests.py | tee results/tests.log
.venv/bin/python crawler.py config.yaml | tee results/crawl-run.log
.venv/bin/python scripts/stats.py config.yaml --tex | tee results/stats.tsv
cd report && pdflatex -interaction=nonstopmode report.tex >/dev/null && pdflatex -interaction=nonstopmode report.tex >/dev/null
