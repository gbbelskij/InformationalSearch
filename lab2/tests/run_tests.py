#!/usr/bin/env python3
"""Автотесты поискового робота.

Поднимают локальный HTTP-сервер с маленьким тестовым сайтом и запускают
crawler.py как утилиту командной строки с тестовыми конфигами.
"""
import functools
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from collections import Counter
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

import yaml
from pymongo import MongoClient

HERE = os.path.dirname(os.path.abspath(__file__))
CRAWLER = os.path.join(HERE, "..", "crawler.py")
sys.path.insert(0, os.path.join(HERE, ".."))
from crawler import normalize  # noqa: E402

with open(os.path.join(HERE, "..", "config.yaml"), encoding="utf-8") as f:
    URI = yaml.safe_load(f)["db"]["uri"]
NODES = ["127.0.0.1:27017", "127.0.0.1:27018", "127.0.0.1:27019"]

# Тестовый сайт: ссылки в разных формах, цикл, дубликат по canonical,
# страница под запретом robots.txt, внешняя ссылка и битая ссылка.
SITE = {
    "robots.txt": "User-agent: *\nDisallow: /private/\n",
    "index.html": '<html><body><a href="a.html">A</a> <a href="./b.html#top">B</a> '
                  '<a href="/dir/../c.html">C</a> <a href="d.html">D</a> '
                  '<a href="https://example.com/x.html">ext</a></body></html>',
    "a.html": '<html><body><a href="/index.html">home</a> <a href="b.html">B</a></body></html>',
    "b.html": '<html><body><p>version 1</p></body></html>',
    "c.html": '<html><body><a href="private/secret.html">secret</a> '
              '<a href="missing.html">404</a></body></html>',
    "d.html": '<html><head><link rel="canonical" href="/b.html"></head>'
              '<body><p>version 1</p></body></html>',
    "private/secret.html": '<html><body>secret</body></html>',
}
REQUESTS = []  # (путь, код ответа) -- журнал тестового сервера
FAILED = []


class Handler(SimpleHTTPRequestHandler):
    def log_request(self, code="-", size="-"):
        REQUESTS.append((self.path, int(code)))

    def log_message(self, *args):
        pass


def check(cond, text):
    print(f"  [{'ok' if cond else 'FAIL'}] {text}")
    if not cond:
        FAILED.append(text)


def db_name(tmp):
    return "test_" + os.path.basename(tmp).replace("-", "_")


def write_config(tmp, base, **logic):
    cfg = {
        "db": {"uri": URI, "name": db_name(tmp)},
        "logic": {"delay": 0, "recrawl_after": 3600, "timeout": 5, **logic},
        "sources": [{"name": "testsite", "seeds": [base + "/index.html"],
                     "follow": "^" + base.replace(".", r"\.") + "/",
                     "save": "^" + base.replace(".", r"\.") + "/"}],
    }
    path = os.path.join(tmp, "config.yaml")
    with open(path, "w", encoding="utf-8") as f:
        yaml.safe_dump(cfg, f, allow_unicode=True)
    return path


def show(output):
    for line in output.splitlines():
        print("    | " + line)


def crawl(cfg):
    """Запуск робота как утилиты; его журнал выводится с отступом."""
    r = subprocess.run([sys.executable, CRAWLER, cfg], capture_output=True, text=True, timeout=120)
    show(r.stdout + r.stderr)
    return r.returncode


def documents(tmp):
    with MongoClient(URI) as client:
        db = client[db_name(tmp)]
        docs = {d["_id"]: (d["source"], d["html"], d["fetched_at"], d["checked_at"]) for d in db.documents.find()}
        frontier = {f["_id"]: f["status"] for f in db.frontier.find()}
    return docs, frontier


def test_normalize():
    base = "https://site.com/path/page.html"
    cases = [  # правила разрешения URL из лекции + приведение к одному виду
        ("http://wikipedia.org", base, "http://wikipedia.org/"),
        ("//cdn.org/jquery.js", base, "https://cdn.org/jquery.js"),
        ("/admin/index.html", base, "https://site.com/admin/index.html"),
        ("another.html", base, "https://site.com/path/another.html"),
        ("?full=1", base, "https://site.com/path/page.html?full=1"),
        ("#chapter2", base, "https://site.com/path/page.html"),
        ("../up/./x.html", base, "https://site.com/up/x.html"),
        ("HTTP://Site.COM:80/S%c3%a3o_Paulo#x", None, "http://site.com/S%C3%A3o_Paulo"),
        ("https://site.com/São_Paulo", None, "https://site.com/S%C3%A3o_Paulo"),
        ("https://site.com/Travelers%27_diarrhea", None, "https://site.com/Travelers'_diarrhea"),
        ("https://site.com:8443/p?b=2&a=1", None, "https://site.com:8443/p?a=1&b=2"),
        ("mailto:a@site.com", base, None),
        ("javascript:void(0)", base, None),
    ]
    for url, b, expected in cases:
        got = normalize(url, b)
        check(got == expected, f"{url!r} -> {got!r}" + ("" if got == expected else f", ожидалось {expected!r}"))


def test_crawl(tmp, base, expected):
    t0 = int(time.time())
    REQUESTS.clear()
    check(crawl(write_config(tmp, base)) == 0, "робот завершился с кодом 0")
    docs, frontier = documents(tmp)
    check(set(docs) == expected, "сохранены ровно index, a, b, c: " + " ".join(sorted(u[len(base):] for u in docs)))
    check(all(d[0] == "testsite" and isinstance(d[2], int) and t0 <= d[2] <= time.time() for d in docs.values()),
          "у документов есть источник и дата обкачки (Unix time)")
    check(docs.get(base + "/b.html", ("",) * 2)[1] == SITE["b.html"], "сохранён сырой HTML без изменений")
    count = Counter(path for path, _ in REQUESTS)
    check(all(count[p] == 1 for p in ("/index.html", "/a.html", "/b.html", "/c.html")),
          "каждая страница скачана один раз, хотя ссылки на неё записаны по-разному")
    check(frontier.get(base + "/d.html") == "dup" and base + "/d.html" not in docs,
          "d.html (canonical = b.html) не сохранена как отдельный документ")
    check("/private/secret.html" not in count, "страница, запрещённая в robots.txt, не скачивалась")
    check(not any("example.com" in u for u in frontier), "внешняя ссылка не попала в очередь")
    check(frontier.get(base + "/missing.html") == "error", "битая ссылка (404) помечена как error")


def test_resume(tmp, base, expected):
    REQUESTS.clear()
    cfg = write_config(tmp, base, delay=1)
    p = subprocess.Popen([sys.executable, CRAWLER, cfg], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    deadline = time.time() + 30
    while time.time() < deadline:
        time.sleep(0.1)
        if len(documents(tmp)[0]) >= 2:
            break
    p.kill()  # аварийная остановка (SIGKILL), без корректного завершения
    show(p.communicate()[0] + "<kill -9>")
    before = set(documents(tmp)[0])
    check(0 < len(before) < len(expected), f"до остановки сохранено {len(before)} из {len(expected)} документов")
    REQUESTS.clear()
    check(crawl(write_config(tmp, base)) == 0, "повторный запуск завершился с кодом 0")
    after = set(documents(tmp)[0])
    check(after == expected, "после продолжения сохранены все документы")
    again = {base + path for path, _ in REQUESTS} & before
    check(not again, "документы, сохранённые до остановки, повторно не скачивались")


def test_recheck_unchanged(tmp, base):
    old, _ = documents(tmp)
    time.sleep(1.1)  # checked_at в секундах: проверки прошлого запуска должны быть раньше нового
    REQUESTS.clear()
    check(crawl(write_config(tmp, base, recrawl_after=0)) == 0, "робот завершился с кодом 0")
    new, _ = documents(tmp)
    codes = {base + path: code for path, code in REQUESTS if path != "/robots.txt"}
    check(set(codes) == set(old) and set(codes.values()) == {304},
          "все документы перепроверены условным GET, сервер ответил 304")
    check(all(new[u][1:3] == old[u][1:3] for u in old), "html и дата обкачки не изменились")
    check(all(new[u][3] > old[u][3] for u in old), "дата проверки (checked_at) обновилась")


def test_recheck_changed(tmp, base, site):
    old, _ = documents(tmp)
    path = os.path.join(site, "b.html")
    with open(path, "w", encoding="utf-8") as f:
        f.write(SITE["b.html"].replace("version 1", "version 2"))
    os.utime(path, (time.time() + 10, time.time() + 10))  # Last-Modified строго новее прежнего
    time.sleep(1.1)
    REQUESTS.clear()
    check(crawl(write_config(tmp, base, recrawl_after=0)) == 0, "робот завершился с кодом 0")
    new, _ = documents(tmp)
    codes = {base + path: code for path, code in REQUESTS if path != "/robots.txt"}
    b = base + "/b.html"
    check(codes.get(b) == 200 and "version 2" in new[b][1] and new[b][2] > old[b][2],
          "изменённая b.html скачана заново, html и дата обкачки обновлены")
    check(all(codes.get(u) == 304 and new[u][1:3] == old[u][1:3] for u in old if u != b),
          "остальные документы не изменились (304)")


def test_replicas(tmp, expected):
    with MongoClient(URI) as client:
        members = {m["name"]: m["stateStr"] for m in client.admin.command("replSetGetStatus")["members"]}
    check(sorted(members) == NODES and sorted(members.values()) == ["PRIMARY", "SECONDARY", "SECONDARY"],
          "набор реплик rs0: " + ", ".join(f"{n} {s}" for n, s in sorted(members.items())))
    counts = {}
    deadline = time.time() + 10
    while time.time() < deadline:
        for node in NODES:
            with MongoClient(node, directConnection=True, readPreference="secondaryPreferred") as c:
                counts[node] = set(c[db_name(tmp)].documents.distinct("_id"))
        if all(v == expected for v in counts.values()):
            break
        time.sleep(0.2)
    check(all(v == expected for v in counts.values()),
          "документы, записанные роботом, есть на каждом из трёх узлов: " +
          ", ".join(f"{n} — {len(v)}" for n, v in sorted(counts.items())))


def main():
    site = tempfile.mkdtemp(prefix="lab2-site-")
    for name, text in SITE.items():
        os.makedirs(os.path.dirname(os.path.join(site, name)), exist_ok=True)
        with open(os.path.join(site, name), "w", encoding="utf-8") as f:
            f.write(text)
    server = ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, directory=site))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    expected = {base + p for p in ("/index.html", "/a.html", "/b.html", "/c.html")}
    dirs = [tempfile.mkdtemp(prefix="lab2-test-") for _ in range(2)]

    tests = [
        ("Нормализация URL", test_normalize),
        ("Обход тестового сайта", lambda: test_crawl(dirs[0], base, expected)),
        ("Аварийная остановка и продолжение", lambda: test_resume(dirs[1], base, expected)),
        ("Переобкачка: документы не изменились", lambda: test_recheck_unchanged(dirs[0], base)),
        ("Переобкачка: один документ изменился", lambda: test_recheck_changed(dirs[0], base, site)),
        ("Распределённая база: копии на всех узлах", lambda: test_replicas(dirs[0], expected)),
    ]
    results = []
    for i, (name, fn) in enumerate(tests, 1):
        print(f"=== Тест {i}. {name} ===")
        failed = len(FAILED)
        fn()
        results.append(len(FAILED) == failed)
        print(f"--- Тест {i}: {'OK' if results[-1] else 'FAIL'}\n")
    server.shutdown()
    with MongoClient(URI) as client:
        for d in dirs:
            client.drop_database(db_name(d))
    for d in dirs + [site]:
        shutil.rmtree(d)
    print(f"Итого: пройдено {sum(results)} из {len(results)} тестов")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
