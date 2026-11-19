#!/usr/bin/env python3
import os
import sqlite3
import sys

import yaml
from pymongo import ReplaceOne

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from crawler import connect, ensure_indexes  # noqa: E402


def flush(collection, batch):
    if batch:
        collection.bulk_write(batch, ordered=False)
        batch.clear()


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: migrate_sqlite.py <config.yaml> <crawler.db>")
    with open(sys.argv[1], encoding="utf-8") as f:
        cfg = yaml.safe_load(f)
    sq = sqlite3.connect(sys.argv[2])
    client, db = connect(cfg["db"])
    ensure_indexes(db)

    batch = []
    for url, source, html, fetched_at, checked_at, last_modified, etag, h in sq.execute(
            "SELECT url, source, html, fetched_at, checked_at, last_modified, etag, hash FROM documents"):
        batch.append(ReplaceOne({"_id": url}, {
            "url": url, "source": source, "html": html, "fetched_at": fetched_at, "checked_at": checked_at,
            "last_modified": last_modified, "etag": etag, "hash": h}, upsert=True))
        if len(batch) == 100:
            flush(db.documents, batch)
    flush(db.documents, batch)

    for seq, url, source, depth, status in sq.execute("SELECT id, url, source, depth, status FROM frontier"):
        batch.append(ReplaceOne({"_id": url}, {"source": source, "depth": depth, "status": status, "seq": seq},
                                upsert=True))
        if len(batch) == 5000:
            flush(db.frontier, batch)
    flush(db.frontier, batch)

    for table in ("documents", "frontier"):
        n_sql = sq.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
        n_mongo = db[table].count_documents({})
        print(f"{table}: SQLite {n_sql}, MongoDB {n_mongo}")
    client.close()


if __name__ == "__main__":
    main()
