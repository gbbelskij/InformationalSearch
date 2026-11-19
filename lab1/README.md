# ЛР1. Добыча корпуса документов

Требования: Python 3.8+ (только стандартная библиотека), доступ в интернет, `pdflatex` с пакетами из `report/styles/log-style.sty`.

Запуск всего с нуля: `./run.sh` — первый корпус, ~30000 статей через API (выкачка занимает несколько часов). Основной корпус на 1 млн документов собирается отдельно, `scripts/download_million.py` (ниже; дампы ~26 ГиБ, разбор Wikipedia ~1,5 часа).

- `scripts/download.py N [wikivoyage] [wikipedia]` — выкачка N статей из каждого источника (по умолчанию из обоих) в `data/raw/<источник>/` (HTML + `meta.jsonl`).
- `scripts/extract.py` — выделение текста в `data/text/<источник>/*.txt`.
- `scripts/stats.py [каталог таблица]` — статистика корпуса: `stats.py` — первый корпус (`results/stats.tsv`), `stats.py data_1m stats-1m-table.tex` — миллион (`results/stats-1m.tsv`); таблица для отчёта.
- `scripts/search_examples.py` — примеры запросов к встроенному поиску (`results/search_examples.txt`).
- `report/` — отчёт (LaTeX), `report/report.pdf`.

Корпус на 1 млн документов (дампы вместо API):

```
python3 -m venv .venv && .venv/bin/pip install py7zr
.venv/bin/python scripts/download_million.py [--total 1000000] [--sources travelse reddit wikivoyage wikipedia]
```

- `travelse` — Travel Stack Exchange, дамп archive.org (7z, ~156 МБ): вопрос + ответы → HTML-документ.
- `reddit` — текстовые посты r/travel, r/solotravel, r/backpacking из архива Arctic Shift (API, по кварталам
  в `--reddit-threads` потоков), пост → HTML-документ, берутся посты от `--min-words` слов.
- `wikivoyage` — все статьи англ. Wikivoyage из multistream-дампа (~35 тыс.).
- `wikipedia` — англ. Wikipedia из multistream-дампа (~27 ГБ); статьи ранжируются по туристическим словам,
  туристическим категориям и признакам места (координаты, инфобокс места), берутся лучшие до `--total`,
  статьи короче `--min-words` (100) слов отбрасываются.

Результат в `data_1m/` в формате ЛР1: `raw/<источник>/meta.jsonl` + сырьё (`.wiki`/`.html`),
`text/<источник>/<id>.txt`; дампы — в `data_1m/dumps/` (докачка при перезапуске,
готовый источник с `meta.jsonl` повторно не обрабатывается).
