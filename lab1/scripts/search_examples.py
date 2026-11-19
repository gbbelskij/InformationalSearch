#!/usr/bin/env python3
"""Примеры запросов к встроенному поиску Wikivoyage и Wikipedia (CirrusSearch)."""
import html
import re
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from download import api

QUERIES = [
    "cheap hostels in Paris",
    "best time to visit Iceland",
    "visa free countries for Russian citizens",
    "beach holiday with kids",
]
SITES = [("Wikivoyage", "en.wikivoyage.org"), ("Wikipedia", "en.wikipedia.org")]

for q in QUERIES:
    for name, host in SITES:
        r = api(host, action="query", list="search", srsearch=q, srlimit=5,
                srprop="snippet")
        print(f"## {name}: \"{q}\" (всего найдено: {r['query']['searchinfo']['totalhits']})")
        for i, h in enumerate(r["query"]["search"], 1):
            snippet = html.unescape(re.sub(r"<[^>]+>", "", h["snippet"]))[:120]
            print(f"{i}. {h['title']} -- {snippet}")
        print()
