#!/bin/sh
# Полный прогон ЛР1: выкачка -> выделение текста -> статистика -> примеры поиска -> отчёт.
set -e
cd "$(dirname "$0")"
python3 scripts/download.py 15000
python3 scripts/extract.py
python3 scripts/stats.py | tee results/stats.tsv
python3 scripts/search_examples.py > results/search_examples.txt
cd report && pdflatex -interaction=nonstopmode report.tex >/dev/null && pdflatex -interaction=nonstopmode report.tex >/dev/null
