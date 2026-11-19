# ЛР2. Поисковый робот

Требования: Python 3.8+, Docker с `docker compose`, доступ в интернет, `pdflatex` для отчёта. Python-пакеты `pymongo` и `pyyaml` ставятся в окружение `.venv` (`run.sh` создаёт его сам: `python3 -m venv .venv && .venv/bin/pip install pymongo pyyaml`).

База — распределённая: MongoDB 7.0, набор реплик `rs0` из трёх узлов (`docker-compose.yml`, порты 27017–27019, сеть хоста).

- `docker compose up -d` — запустить три узла; `.venv/bin/python scripts/mongo_init.py` — создать набор реплик (повторный запуск только показывает состояние узлов). `docker compose stop` — остановить узлы, данные остаются в томах Docker.
- `.venv/bin/python crawler.py config.yaml` — робот; единственный аргумент — путь к конфигу. Остановка — Ctrl+C (или `kill`), повторный запуск продолжает обход с места остановки и перепроверяет устаревшие документы. Если в окружении задан `HTTPS_PROXY`, запускать через `env -u HTTPS_PROXY -u HTTP_PROXY`.
- `config.yaml` — конфиг: `db` (`uri` набора реплик и `name` базы), `logic` (задержка, период переобкачки, таймаут, User-Agent), `sources` (источники: начальные URL и правила обхода).
- `tests/run_tests.py` — автотесты на локальном тестовом сайте (`results/tests.log`); нужен запущенный набор реплик, каждый тест пишет во временную базу.
- `scripts/stats.py config.yaml [--tex]` — статистика базы (`results/stats.tsv`); с `--tex` также пишет таблицу для отчёта.
- `scripts/migrate_sqlite.py config.yaml data/crawler.db` — перенос данных из прежней базы SQLite (пробный прогон) в MongoDB.
- `scripts/failover_demo.py config.yaml` — демонстрация отказа: робот работает, основной узел базы останавливается и возвращается (`results/failover.log`).
- `run.sh` — всё подряд: окружение → узлы базы → тесты → обход → статистика → отчёт.
- `results/crawl-*.log` — журналы пробного прогона; `report/` — отчёт (LaTeX), `report/report.pdf`.

Коллекции базы `crawler`: `documents` (`_id` = url, source, html, fetched_at, checked_at, last_modified, etag, hash) и очередь `frontier` (`_id` = url, source, depth, status, seq).
