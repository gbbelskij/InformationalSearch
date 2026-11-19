#!/usr/bin/env python3
"""Поисковый робот: обход сайтов по ссылкам, сохранение страниц в MongoDB (набор реплик).

Использование: crawler.py <config.yaml>

Каждый источник из конфига обходится отдельным потоком в ширину от начальных URL.
Очередь (frontier) и документы хранятся в базе, поэтому после остановки
(Ctrl+C, SIGTERM или даже kill -9) повторный запуск продолжает с того же места.
Документы, проверенные больше logic.recrawl_after секунд назад, перепроверяются
условным GET (If-Modified-Since) и перезаписываются, только если изменились.
"""
import gzip
import hashlib
import http.client
import re
import signal
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import urllib.robotparser
from collections import Counter
from html.parser import HTMLParser

import yaml
from pymongo import ASCENDING, MongoClient, UpdateOne

SEQ_LOCK = threading.Lock()
SEQ_LAST = [0]


def next_seq():
    with SEQ_LOCK:
        SEQ_LAST[0] = max(SEQ_LAST[0] + 1, time.time_ns())
        return SEQ_LAST[0]


def connect(cfg):
    client = MongoClient(cfg["uri"], w="majority")
    return client, client[cfg["name"]]


def ensure_indexes(db):
    db.documents.create_index([("source", ASCENDING), ("checked_at", ASCENDING)])
    db.frontier.create_index([("source", ASCENDING), ("status", ASCENDING), ("depth", ASCENDING), ("seq", ASCENDING)])


def enqueue(url, source, depth):
    return UpdateOne({"_id": url}, {"$setOnInsert": {"source": source, "depth": depth, "status": "new",
                                                     "seq": next_seq()}}, upsert=True)


def normalize(url, base=None):
    """Абсолютный нормализованный URL или None, если ссылка не http(s)."""
    try:
        p = urllib.parse.urlsplit(urllib.parse.urljoin(base or "", url.strip()))
        port = p.port
    except ValueError:
        return None
    scheme = p.scheme.lower()
    if scheme not in ("http", "https") or not p.hostname:
        return None
    host = p.hostname  # уже в нижнем регистре, без логина и пароля
    if port and port != (80 if scheme == "http" else 443):
        host += f":{port}"
    path = urllib.parse.quote(urllib.parse.unquote(p.path), safe="/:@!$&'()*+,;=~") or "/"
    query = urllib.parse.urlencode(sorted(urllib.parse.parse_qsl(p.query, keep_blank_values=True)))
    return urllib.parse.urlunsplit((scheme, host, path, query, ""))


def decode(body, content_type):
    """Кодировка: из Content-Type, затем из <meta charset>, иначе UTF-8."""
    m = re.search(r"charset=[\"']?([\w-]+)", content_type, re.I)
    charset = m.group(1) if m else None
    if not charset:
        m = re.search(rb"<meta[^>]+charset=[\"']?([\w-]+)", body[:4096], re.I)
        charset = m.group(1).decode("ascii") if m else "utf-8"
    try:
        return body.decode(charset, errors="replace")
    except LookupError:
        return body.decode("utf-8", errors="replace")


class LinkParser(HTMLParser):
    """Ссылки <a href> внутри элемента с id=scope (или всей страницы) и <link rel=canonical>."""

    def __init__(self, scope=None):
        super().__init__()
        self.scope, self.scope_tag, self.level = scope, None, 0
        self.links, self.canonical = [], None

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "link" and a.get("rel") == "canonical" and a.get("href"):
            self.canonical = a["href"]
        if self.scope_tag:
            self.level += tag == self.scope_tag
        elif self.scope and a.get("id") == self.scope:
            self.scope_tag, self.level = tag, 1
        if tag == "a" and a.get("href") and (not self.scope or self.scope_tag):
            self.links.append(a["href"])

    def handle_endtag(self, tag):
        if tag == self.scope_tag:
            self.level -= 1
            if self.level == 0:
                self.scope_tag = None


class Robot(threading.Thread):
    """Обход одного источника."""

    def __init__(self, src, logic, client, db, stop):
        super().__init__(name=src["name"])
        self.seeds = src.get("seeds", [])
        self.follow = re.compile(src["follow"]) if src.get("follow") else None
        self.save = re.compile(src["save"]) if src.get("save") else None
        self.links_in = src.get("links_in")
        self.max_depth = src.get("max_depth")
        self.limit = src.get("limit")
        self.delay = float(logic.get("delay", 1))
        self.recrawl_after = logic.get("recrawl_after")
        self.timeout = logic.get("timeout", 60)
        self.ua = logic.get("user_agent", "MAI-IR-lab2/1.0")
        self.client, self.db, self.stop = client, db, stop
        self.robots = {}
        self.stats = Counter()
        self.started = int(time.time())

    def log(self, event, url, extra=""):
        self.stats[event] += 1
        print(f"{time.strftime('%H:%M:%S')} {self.name} {event} {url} {extra}".rstrip(), flush=True)

    def get(self, url, headers=None):
        """GET с повторами при сетевых ошибках, 429 и 5xx: (код, итоговый URL, заголовки, тело)."""
        req = urllib.request.Request(url, headers={
            "User-Agent": self.ua, "Accept-Encoding": "gzip", **(headers or {})})
        for attempt in range(3):
            try:
                with urllib.request.urlopen(req, timeout=self.timeout) as r:
                    body = r.read()
                    if r.headers.get("Content-Encoding") == "gzip":
                        body = gzip.decompress(body)
                    return r.status, r.geturl(), r.headers, body
            except urllib.error.HTTPError as e:
                if e.code != 429 and e.code < 500:
                    return e.code, url, e.headers, None
                err = e
            except (OSError, http.client.HTTPException) as e:
                err = e
            print(f"{self.name} retry {attempt + 1}: {url}: {err}", file=sys.stderr, flush=True)
            if self.stop.wait(5 * (attempt + 1)):
                break
        return 0, url, None, None

    def allowed(self, url):
        """Проверка по robots.txt хоста; файл скачивается один раз за запуск."""
        p = urllib.parse.urlsplit(url)
        root = f"{p.scheme}://{p.netloc}"
        rp = self.robots.get(root)
        if rp is None:
            rp = self.robots[root] = urllib.robotparser.RobotFileParser()
            code, _, _, body = self.get(root + "/robots.txt")
            # Как в urllib.robotparser: 401/403 -- всё запрещено, файла нет -- всё разрешено.
            if code == 200:
                rp.parse(body.decode("utf-8", "replace").splitlines())
            elif code in (401, 403):
                rp.disallow_all = True
            else:
                rp.allow_all = True
            self.delay = max(self.delay, float(rp.crawl_delay(self.ua) or 0))
        return rp.can_fetch(self.ua, url)

    def wanted(self, url):
        return any(r and r.search(url) for r in (self.follow, self.save)) and self.allowed(url)

    def next_task(self):
        """Сначала документы, которые пора перепроверить, затем очередь в ширину."""
        if self.recrawl_after is not None:
            # Документ перепроверяется не чаще раза за запуск -- так обход всегда завершается.
            before = min(int(time.time()) - self.recrawl_after, self.started)
            row = self.db.documents.find_one({"source": self.name, "checked_at": {"$lt": before}},
                                             {"_id": 1}, sort=[("checked_at", ASCENDING)])
            if row:
                return self.recheck, row["_id"]
        saved = self.db.documents.count_documents({"source": self.name})
        if self.limit is None or saved < self.limit:
            row = self.db.frontier.find_one({"source": self.name, "status": "new"}, {"depth": 1},
                                            sort=[("depth", ASCENDING), ("seq", ASCENDING)])
            if row:
                return self.crawl, row["_id"], row["depth"]
        return None

    def crawl(self, url, depth):
        code, final, headers, body = self.get(url)
        if code != 200 or headers.get_content_type() != "text/html":
            self.db.frontier.update_one({"_id": url}, {"$set": {"status": "error"}})
            self.log("error", url, str(code))
            return
        html = decode(body, headers.get("Content-Type", ""))
        page = LinkParser(self.links_in)
        page.feed(html)
        final = normalize(final)
        # Перенаправления MediaWiki отдают ту же статью по другому URL -- берём канонический.
        doc_url = page.canonical and normalize(page.canonical, final) or final
        now = int(time.time())
        links = []
        if self.follow and self.follow.search(doc_url) and (self.max_depth is None or depth < self.max_depth):
            for href in page.links:
                u = normalize(href, final)
                if u and u not in links and self.wanted(u):
                    links.append(u)
        result = {}

        def save(session):
            status, event, added = "done", "visited", 0
            if self.save and self.save.search(doc_url):
                if self.db.documents.find_one({"_id": doc_url}, {"_id": 1}, session=session):
                    status = event = "dup"
                else:
                    self.db.documents.insert_one({
                        "_id": doc_url, "url": doc_url, "source": self.name, "html": html, "fetched_at": now,
                        "checked_at": now, "last_modified": headers.get("Last-Modified"),
                        "etag": headers.get("ETag"), "hash": hashlib.sha1(html.encode()).hexdigest()},
                        session=session)
                    self.db.frontier.bulk_write([enqueue(doc_url, self.name, depth)], session=session)
                    self.db.frontier.update_one({"_id": doc_url}, {"$set": {"status": "done"}}, session=session)
                    event = "saved"
            if links:
                added = self.db.frontier.bulk_write([enqueue(u, self.name, depth + 1) for u in links],
                                                    ordered=False, session=session).upserted_count
            self.db.frontier.update_one({"_id": url}, {"$set": {"status": status}}, session=session)
            result.update(event=event, added=added)

        with self.client.start_session() as session:
            session.with_transaction(save)
        self.log(result["event"], url, f"depth={depth} new_links={result['added']}" +
                 (f" -> {doc_url}" if doc_url != url else ""))

    def recheck(self, url):
        doc = self.db.documents.find_one({"_id": url}, {"last_modified": 1, "etag": 1, "hash": 1})
        last_modified, etag, old_hash = doc.get("last_modified"), doc.get("etag"), doc["hash"]
        cond = {}
        if last_modified:
            cond["If-Modified-Since"] = last_modified
        if etag:
            cond["If-None-Match"] = etag
        code, _, headers, body = self.get(url, cond)
        now = int(time.time())
        event = "not-modified" if code == 304 else f"error {code}"
        if code == 200:
            html = decode(body, headers.get("Content-Type", ""))
            new_hash = hashlib.sha1(html.encode()).hexdigest()
            fields = {"checked_at": now, "last_modified": headers.get("Last-Modified"), "etag": headers.get("ETag")}
            if new_hash != old_hash:
                fields.update(html=html, hash=new_hash, fetched_at=now)
                event = "changed"
            else:
                event = "unchanged"
            self.db.documents.update_one({"_id": url}, {"$set": fields})
        else:
            self.db.documents.update_one({"_id": url}, {"$set": {"checked_at": now}})
        self.log(event, url)

    def run(self):
        seeds = [enqueue(u, self.name, 0) for u in map(normalize, self.seeds) if u and self.allowed(u)]
        if seeds:
            self.db.frontier.bulk_write(seeds, ordered=False)
        while not self.stop.is_set():
            task = self.next_task()
            if task is None:
                break
            try:
                task[0](*task[1:])
            except Exception as e:  # битая страница не должна останавливать обход
                self.db.frontier.update_one({"_id": task[1]}, {"$set": {"status": "error"}})
                self.db.documents.update_one({"_id": task[1]}, {"$set": {"checked_at": int(time.time())}})
                self.log("error", task[1], repr(e))
            self.stop.wait(self.delay)
        self.total = self.db.documents.count_documents({"source": self.name})


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: crawler.py <config.yaml>")
    with open(sys.argv[1], encoding="utf-8") as f:
        cfg = yaml.safe_load(f)
    client, db = connect(cfg["db"])
    ensure_indexes(db)

    stop = threading.Event()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: stop.set())
    robots = [Robot(src, cfg.get("logic", {}), client, db, stop) for src in cfg["sources"]]
    started = time.time()
    for r in robots:
        r.start()
    for r in robots:
        while r.is_alive():
            r.join(0.5)
    print(("остановлен" if stop.is_set() else "обход завершён") + f" за {time.time() - started:.0f} с")
    for r in robots:
        print(f"{r.name}: " + " ".join(f"{k}={v}" for k, v in sorted(r.stats.items())) +
              f" | документов в базе: {getattr(r, 'total', '?')}")
    client.close()


if __name__ == "__main__":
    main()
