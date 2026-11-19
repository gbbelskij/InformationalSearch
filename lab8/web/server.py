#!/usr/bin/env python3
import argparse
import html
import os
import subprocess
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = 50

STYLE = """body{font-family:sans-serif;max-width:860px;margin:24px auto;padding:0 16px;color:#222}
form{margin:16px 0}input[name=q]{width:70%;font-size:16px;padding:6px}button{font-size:16px;padding:6px 14px}
li{margin:6px 0}.url{color:#2a7a2a;font-size:13px}.info{color:#666}.nav a{margin-right:16px}"""


class Engine:
    def __init__(self, binary, index):
        self.proc = subprocess.Popen([binary, index, "-p"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     text=True, encoding="utf-8", bufsize=1)
        self.lock = threading.Lock()

    def search(self, query, offset, limit):
        query = " ".join(query.split())
        with self.lock:
            self.proc.stdin.write(f"{offset}\t{limit}\t{query}\n")
            self.proc.stdin.flush()
            lines = []
            while True:
                line = self.proc.stdout.readline()
                if not line:
                    raise RuntimeError("search process exited")
                line = line.rstrip("\n")
                if not line:
                    break
                lines.append(line)
        head = dict(line.split(": ", 1) if ": " in line else (line.rstrip(":"), "") for line in lines[:4])
        results = [line.split("\t") for line in lines[4:]]
        return int(head["found"]), float(head["time_us"]), head["parsed"], results


def form(query=""):
    return (f'<form action="/search" method="get"><input name="q" value="{html.escape(query)}" autofocus> '
            '<button type="submit">Найти</button></form>')


def page(title, body):
    return (f'<!doctype html><html lang="ru"><head><meta charset="utf-8"><title>{html.escape(title)}</title>'
            f'<style>{STYLE}</style></head><body><h1><a href="/">Поиск по туризму</a></h1>{body}</body></html>')


def make_handler(engine):
    class Handler(BaseHTTPRequestHandler):
        def send(self, code, text):
            data = text.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            url = urllib.parse.urlsplit(self.path)
            if url.path == "/":
                self.send(200, page("Поиск", form() + '<p class="info">Wikipedia, Reddit, Travel Stack Exchange и Wikivoyage, 1 000 000 документов. '
                                    'Операторы: пробел или &amp;&amp; — И, || — ИЛИ, ! — НЕ, скобки. '
                                    'Если индекс построен с частотами (ЛР13), выдача ранжируется по TF-IDF, '
                                    'а запрос из одних слов без операторов — нечёткий.</p>'))
            elif url.path == "/search":
                params = urllib.parse.parse_qs(url.query)
                query = params.get("q", [""])[0]
                try:
                    p = max(0, int(params.get("p", ["0"])[0]))
                except ValueError:
                    p = 0
                found, us, parsed, results = engine.search(query, p * PAGE, PAGE)
                items = "".join(f'<li><a href="{html.escape(r[3])}">{html.escape(r[2])}</a><br>'
                                f'<span class="url">{html.escape(r[3])}</span>'
                                + (f' <span class="info">tf-idf {html.escape(r[4])}</span>' if len(r) > 4 else "")
                                + '</li>' for r in results)
                nav = []
                if p > 0:
                    nav.append(f'<a href="/search?{urllib.parse.urlencode({"q": query, "p": p - 1})}">'
                               f'Предыдущие {PAGE}</a>')
                if (p + 1) * PAGE < found:
                    nav.append(f'<a href="/search?{urllib.parse.urlencode({"q": query, "p": p + 1})}">'
                               f'Следующие {PAGE}</a>')
                shown = f"{p * PAGE + 1}–{p * PAGE + len(results)}" if results else "0"
                body = (form(query) + f'<p class="info">Запрос: {html.escape(parsed) or "пустой"}. '
                        f'Найдено документов: {found}, показаны {shown}. Время: {us / 1000:.3f} мс.</p>'
                        f'<ol start="{p * PAGE + 1}">{items}</ol><p class="nav">{"".join(nav)}</p>')
                self.send(200, page(f"{query} — поиск", body))
            else:
                self.send(404, page("Не найдено", "<p>Страница не найдена.</p>"))

        def log_message(self, fmt, *args):
            pass

    return Handler


def main():
    ap = argparse.ArgumentParser(description="Веб-интерфейс булева поиска")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--index", default=os.path.join(HERE, "..", "..", "lab7", "data", "index.bin"))
    ap.add_argument("--search", default=os.path.join(HERE, "..", "build", "search"))
    args = ap.parse_args()
    engine = Engine(args.search, args.index)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(engine))
    print(f"http://{args.host}:{server.server_address[1]}/", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        engine.proc.stdin.close()
        engine.proc.wait()


if __name__ == "__main__":
    main()
