#!/usr/bin/env python3
"""Выкачивание примеров документов из Wikivoyage и Wikipedia через MediaWiki API.

Сырой документ -- HTML статьи (index.php?action=render), метаданные
(id, ревизия, URL, категории) пишутся в meta.jsonl.
Использование: download.py <число документов на источник> [wikivoyage] [wikipedia]
"""
import gzip
import http.client
import json
import os
import sys
import threading
import time
import urllib.parse
from concurrent.futures import ThreadPoolExecutor

UA = "MAI-IR-lab1/1.0 (student coursework; g.belskiy@f6.security)"
ROOT = os.path.join(os.path.dirname(__file__), "..", "data", "raw")
local = threading.local()


def get(url):
    u = urllib.parse.urlsplit(url)
    for attempt in range(8):
        if getattr(local, "host", None) != u.netloc:
            local.conn, local.host = http.client.HTTPSConnection(u.netloc, timeout=120), u.netloc
        try:
            local.conn.request("GET", f"{u.path}?{u.query}",
                               headers={"User-Agent": UA, "Accept-Encoding": "gzip"})
            r = local.conn.getresponse()
            data = r.read()
            if r.status == 200:
                if r.getheader("Content-Encoding") == "gzip":
                    data = gzip.decompress(data)
                return data.decode("utf-8")
            err = f"HTTP {r.status}"
        except Exception as e:
            local.conn.close()
            err = repr(e)
        print(f"retry {attempt}: {err}", file=sys.stderr)
        time.sleep(30 * (attempt + 1) if err == "HTTP 429" else 2 + attempt * 3)
    return None


def api(host, **params):
    params["format"] = "json"
    return json.loads(get(f"https://{host}/w/api.php?" + urllib.parse.urlencode(params)))


def wikivoyage_titles(n):
    """Первые n статей основного пространства имён (без перенаправлений)."""
    titles, cont = [], {}
    while len(titles) < n:
        r = api("en.wikivoyage.org", action="query", list="allpages",
                apnamespace=0, apfilterredir="nonredirects", aplimit=500, **cont)
        titles += [p["title"] for p in r["query"]["allpages"]]
        if "continue" not in r:
            break
        cont = {"apcontinue": r["continue"]["apcontinue"]}
    return titles[:n]


def wikipedia_titles(n, root="Category:Tourism", max_depth=3):
    """Обход дерева категорий в ширину от Category:Tourism."""
    titles, seen_t, seen_c = [], set(), {root}
    queue = [(root, 0)]
    while queue and len(titles) < n:
        cat, depth = queue.pop(0)
        cont = {}
        while True:
            r = api("en.wikipedia.org", action="query", list="categorymembers",
                    cmtitle=cat, cmtype="page|subcat", cmlimit=500, **cont)
            for m in r["query"]["categorymembers"]:
                if m["ns"] == 0 and m["title"] not in seen_t:
                    seen_t.add(m["title"])
                    titles.append(m["title"])
                elif m["ns"] == 14 and depth < max_depth and m["title"] not in seen_c:
                    seen_c.add(m["title"])
                    queue.append((m["title"], depth + 1))
            if "continue" not in r:
                break
            cont = {"cmcontinue": r["continue"]["cmcontinue"]}
    return titles[:n]


def metadata(host, titles):
    """id, ревизия, URL и видимые категории -- пакетами по 50 статей."""
    metas = []
    for i in range(0, len(titles), 50):
        pages, cont = {}, {}
        while True:
            r = api(host, action="query", titles="|".join(titles[i:i + 50]),
                    prop="info|categories", cllimit="max", clshow="!hidden",
                    formatversion=2, **cont)
            for p in r["query"]["pages"]:
                if "missing" in p:
                    continue
                if p["pageid"] not in pages:
                    pages[p["pageid"]] = {
                        "id": p["pageid"], "title": p["title"], "revid": p["lastrevid"],
                        "url": f"https://{host}/wiki/" + urllib.parse.quote(p["title"].replace(" ", "_")),
                        "categories": [], "file": f"{p['pageid']}.html"}
                m = pages[p["pageid"]]
                m["categories"] += [c["title"].split(":", 1)[1] for c in p.get("categories", [])]
            if "continue" not in r:
                break
            cont = r["continue"]
        metas += pages.values()
    return metas


def fetch(source, host, titles):
    out = os.path.join(ROOT, source)
    os.makedirs(out, exist_ok=True)

    def work(m):
        html = get(f"https://{host}/w/index.php?" +
                   urllib.parse.urlencode({"title": m["title"], "action": "render"}))
        if html is None:
            return None
        with open(os.path.join(out, m["file"]), "w", encoding="utf-8") as f:
            f.write(html)
        return m

    metas = metadata(host, titles)
    with ThreadPoolExecutor(4) as ex, \
            open(os.path.join(out, "meta.jsonl"), "w", encoding="utf-8") as meta:
        for i, m in enumerate(ex.map(work, metas)):
            if m:
                meta.write(json.dumps(m, ensure_ascii=False) + "\n")
            if i % 100 == 0:
                print(f"{source}: {i}/{len(metas)}", file=sys.stderr)


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    sources = sys.argv[2:] or ["wikivoyage", "wikipedia"]
    if "wikivoyage" in sources:
        fetch("wikivoyage", "en.wikivoyage.org", wikivoyage_titles(n))
    if "wikipedia" in sources:
        fetch("wikipedia", "en.wikipedia.org", wikipedia_titles(n))


if __name__ == "__main__":
    main()
