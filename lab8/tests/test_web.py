#!/usr/bin/env python3
import os
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
INDEXER = os.path.join(HERE, "..", "..", "lab7", "build", "indexer")
SEARCH = os.path.join(HERE, "..", "build", "search")
SERVER = os.path.join(HERE, "..", "web", "server.py")


def build_index(tmp, texts):
    lines = []
    for i, text in enumerate(texts):
        path = os.path.join(tmp, f"{i}.txt")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        lines.append(f"{path}\tDoc {i}\thttps://example.org/{i}\n")
    index = os.path.join(tmp, "index.bin")
    subprocess.run([INDEXER, index], input="".join(lines), capture_output=True, text=True, check=True)
    return index


def get(url):
    try:
        with urllib.request.urlopen(url) as r:
            return r.status, r.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        return e.code, ""


def main():
    print("=== Тест 1. Веб-сервис (web/server.py) ===")
    with tempfile.TemporaryDirectory() as tmp:
        index = build_index(tmp, [f"common word{i}" for i in range(120)])
        proc = subprocess.Popen([sys.executable, SERVER, "--port", "0", "--index", index, "--search", SEARCH],
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        try:
            base = proc.stdout.readline().strip()
            print(f"  сервер: {base}")
            s0, home = get(base)
            s1, p0 = get(base + "search?q=common")
            s2, p2 = get(base + "search?q=common&p=2")
            s3, xss = get(base + "search?" + urllib.parse.urlencode({"q": "<script>alert(1)</script> word5"}))
            s4, bad = get(base + "search?q=common&p=abc")
            s5, _ = get(base + "nothing")
        finally:
            proc.terminate()
            proc.wait()
    checks = [
        (s0 == 200 and '<form action="/search"' in home and 'name="q"' in home, "главная: форма запроса"),
        (s1 == 200 and p0.count("<li>") == 50 and "p=1" in p0 and "Следующие 50" in p0 and "Предыдущие" not in p0
         and 'name="q" value="common"' in p0, "выдача: форма с запросом, 50 результатов, ссылка на следующие 50"),
        (s2 == 200 and p2.count("<li>") == 20 and "Предыдущие 50" in p2 and "Следующие" not in p2
         and '<ol start="101">' in p2, "третья страница: результаты 101–120, без ссылки дальше"),
        (s3 == 200 and "<script>alert" not in xss and "&lt;script&gt;" in xss, "текст запроса экранируется в HTML"),
        (s4 == 200 and bad.count("<li>") == 50, "нечисловой номер страницы — первая страница"),
        (s5 == 404, "неизвестный адрес — 404"),
    ]
    ok = True
    for cond, text in checks:
        print(f"  [{'ok' if cond else 'FAIL'}] {text}")
        ok = ok and cond
    print(f"--- Тест 1: {'OK' if ok else 'FAIL'}\n")
    print(f"Итого: пройдено {int(ok)} из 1")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
