#!/usr/bin/env python3
import argparse
import bz2
import calendar
import html
import json
import multiprocessing
import os
import re
import shutil
import sys
import threading
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed

from extract import extract

UA = "MAI-IR-corpus/1.0 (student coursework)"
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
WIKIS = {"wikivoyage": ("enwikivoyage", "en.wikivoyage.org"),
         "wikipedia": ("enwiki", "en.wikipedia.org")}
SE_URL = "https://archive.org/download/stackexchange/travel.stackexchange.com.7z"
SE_HOST = "travel.stackexchange.com"
REDDIT_API = "https://arctic-shift.photon-reddit.com/api/posts/search"
SUBREDDITS = ("travel", "solotravel", "backpacking")
REDDIT_FIELDS = "id,title,selftext,created_utc,score,link_flair_text"
SOURCES = ("travelse", "reddit", "wikivoyage", "wikipedia")
CHUNK = 8 << 20
SEGMENT = 64 << 20

PAGE = re.compile(rb"<page>.*?</page>", re.S)
HEAD = re.compile(rb"<title>(.*?)</title>\s*<ns>(\d+)</ns>\s*<id>(\d+)</id>(.*?)<revision>\s*<id>(\d+)</id>", re.S)
BODY = re.compile(rb"<text[^>]*?(?:/>|>(.*?)</text>)", re.S)

TOUR = re.compile(rb"touris|sightseeing|\bresorts?\b|\bhotels?\b|\bvisitors?\b|\battractions?\b|\btravel", re.I)
PLACE = re.compile(
    rb"\{\{\s*coord\s*\||\{\{\s*infobox[ _]+(?:settlement|uk place|french commune|italian comune|german location"
    rb"|spanish municipality|swiss town|australian place|indian jurisdiction|u\.?s\.? county|country|islands?"
    rb"|museum|hotel|building|historic building|historic site|historic district|nrhp|landmark|protected area"
    rb"|national park|park|garden|zoo|aquarium|amusement park|theme park|roller coaster|mountain|mountain range"
    rb"|volcano|beach|lake|waterfall|cave|glacier|valley|river|landform|religious building|church|cathedral"
    rb"|mosque|temple|monastery|castle|palace|fort|military structure|monument|memorial|tower|lighthouse|bridge"
    rb"|street|square|archaeological site|world heritage site|ski area|ski resort|resort|casino|restaurant|venue"
    rb"|shopping mall|airport|station|cruise ship|trail|hiking trail|cemetery|geographic location"
    rb"|tourist attraction)\b", re.I)
CATEGORY_B = re.compile(rb"\[\[\s*category\s*:\s*([^\]|]+)", re.I)
TOUR_CAT = re.compile(rb"touris|hotel|resort|attraction|travel|landmark|heritage|visitor|museum|beach|national park", re.I)

CATEGORY = re.compile(r"\[\[\s*category\s*:\s*([^\]|]+)", re.I)
TEMPLATE = re.compile(r"\{\{|\}\}")
TABLE = re.compile(r"(?m)^[ \t]*(\{\||\|\})")
LINK = re.compile(r"\[\[([^\[\]]*)\]\]")
EXTLINK = re.compile(r"\[(?:https?:)?//[^\s\]]*\s*([^\]]*)\]")
TAGBLOCK = re.compile(r"<(ref|gallery|math|timeline|imagemap|score|mapframe|syntaxhighlight|graph|chem)\b"
                      r"[^>]*?(?:/>|>.*?</\1\s*>)", re.S | re.I)
COMMENT = re.compile(r"<!--.*?(?:-->|$)", re.S)
TAG = re.compile(r"</?[a-zA-Z][^>]*>")
HEADING = re.compile(r"(?m)^(=+)\s*(.*?)\s*\1\s*$")
TAIL = re.compile(r"(?mi)^==\s*(?:see also|references|external links|further reading|notes|bibliography"
                  r"|sources|citations|footnotes|notes and references)\s*==\s*$")
LISTMARK = re.compile(r"(?m)^[*#:;]+\s*")
QUOTES = re.compile(r"'{2,}")
MAGIC = re.compile(r"__[A-Z]+__")
SPACES = re.compile(r"[ \t ]+")
MD_LINK = re.compile(r"\[([^\]]*)\]\(([^)\s]*)\)")
MD_MARK = re.compile(r"\*\*|__|~~|\u200b")
MD_LINE = re.compile(r"(?m)^\s*(?:#{1,6}|&gt;|[*+-]|\d+\.)\s+")
SKIP_NS = ("file:", "image:", "category:", "media:")
LISTINGS = {"see", "do", "buy", "eat", "drink", "sleep", "listing", "go", "marker", "vcard", "around"}
PARAM = re.compile(r"\|\s*(?:name|alt|content|description|directions|hours|price)\s*="
                   r"\s*((?:[^|{}\[\]]|\[\[[^\]]*\]\]|\{\{[^{}]*\}\})*)")

SELECTED = frozenset()


def request(url, headers=None):
    return OPENER.open(urllib.request.Request(url, headers={"User-Agent": UA, **(headers or {})}), timeout=60)


def get(url, headers=None):
    for attempt in range(20):
        try:
            with request(url, headers) as r:
                return r.headers, r.read()
        except Exception as e:
            print(f"{url}: {e!r}, retry", file=sys.stderr, flush=True)
            time.sleep(min(60, 5 * (attempt + 1)))
    sys.exit(f"cannot fetch {url}")


def download(url, path, connections):
    if os.path.exists(path):
        return path
    size = int(get(url, {"Range": "bytes=0-0"})[0]["Content-Range"].rsplit("/", 1)[1])
    bounds = list(range(0, size, SEGMENT)) + [size]
    segments = [(bounds[i], bounds[i + 1], f"{path}.{bounds[i]}-{bounds[i + 1]}") for i in range(len(bounds) - 1)]
    done = {name: os.path.getsize(name) if os.path.exists(name) else 0 for _, _, name in segments}
    todo = segments[::-1]

    def fetch(start, end, name):
        for attempt in range(100):
            done[name] = os.path.getsize(name) if os.path.exists(name) else 0
            if start + done[name] >= end:
                return
            try:
                with request(url, {"Range": f"bytes={start + done[name]}-{end - 1}"}) as r, \
                        open(name, "ab") as f:
                    if r.status != 206:
                        raise OSError(f"HTTP {r.status}")
                    t, got = time.time(), 0
                    while block := r.read1(1 << 20):
                        f.write(block)
                        done[name] += len(block)
                        got += len(block)
                        if time.time() - t > 30:
                            if got < 1 << 20:
                                raise OSError("too slow")
                            t, got = time.time(), 0
            except Exception as e:
                print(f"{os.path.basename(name)}: {e!r}, retry", file=sys.stderr, flush=True)
                time.sleep(min(60, 5 * (attempt + 1)))

    def worker():
        while True:
            try:
                segment = todo.pop()
            except IndexError:
                return
            fetch(*segment)

    threads = [threading.Thread(target=worker, daemon=True) for _ in range(connections)]
    for t in threads:
        t.start()
    t0, have = time.time(), sum(done.values())
    while alive := [t for t in threads if t.is_alive()]:
        alive[0].join(30)
        got = sum(done.values())
        print(f"{os.path.basename(path)}: {got >> 20}/{size >> 20} MiB, "
              f"{(got - have) / max(1, time.time() - t0) / 1e6:.1f} MB/s", file=sys.stderr, flush=True)
    if any(done[name] != end - start for start, end, name in segments):
        sys.exit(f"download failed: {url}")
    with open(path + ".tmp", "wb") as out:
        for _, _, name in segments:
            with open(name, "rb") as f:
                shutil.copyfileobj(f, out, 16 << 20)
    os.replace(path + ".tmp", path)
    for _, _, name in segments:
        os.remove(name)
    return path


def wiki_urls(db, dumps):
    local = re.findall(rf"{db}-(\d{{8}})-pages-articles-multistream", " ".join(os.listdir(dumps)))
    if local:
        date = max(local)
    else:
        rss = get(f"https://dumps.wikimedia.org/{db}/latest/{db}-latest-pages-articles-multistream-index.txt.bz2-rss.xml")
        date = re.search(rf"/{db}/(\d{{8}})", rss[1].decode()).group(1)
    base = f"https://dumps.wikimedia.org/{db}/{date}/{db}-{date}-pages-articles-multistream"
    return base + ".xml.bz2", base + "-index.txt.bz2"


def xml_unescape(b):
    return b.replace(b"&lt;", b"<").replace(b"&gt;", b">").replace(b"&quot;", b'"').replace(b"&amp;", b"&")


def pages(path, start, end):
    with open(path, "rb") as f:
        f.seek(start)
        data = bz2.decompress(f.read(end - start))
    for m in PAGE.finditer(data):
        h = HEAD.search(m.group())
        if not h or h.group(2) != b"0" or b"<redirect" in h.group(4):
            continue
        t = BODY.search(m.group(), h.end())
        yield (int(h.group(3)), int(h.group(5)), xml_unescape(h.group(1)).decode("utf-8", "replace"),
               xml_unescape(t.group(1) or b"") if t else b"")


def score(raw):
    s = min(len(TOUR.findall(raw)), 20)
    if PLACE.search(raw):
        s += 3
    if any(TOUR_CAT.search(c) for c in CATEGORY_B.findall(raw)):
        s += 5
    return s


def listing(seg):
    if seg[2:].split("|", 1)[0].strip().lower() not in LISTINGS:
        return ""
    values = [v.strip() for v in PARAM.findall(seg)]
    return "\n" + strip_nested(". ".join(v for v in values if v), TEMPLATE, "{{", listing) + "\n"


def strip_nested(t, pattern, opener, keep=None):
    out, depth, last, begin = [], 0, 0, 0
    for m in pattern.finditer(t):
        if m.group().strip() == opener:
            if depth == 0:
                out.append(t[last:m.start()])
                begin = m.start()
            depth += 1
        elif depth:
            depth -= 1
            if depth == 0:
                if keep:
                    out.append(keep(t[begin:m.end()]))
                last = m.end()
    out.append(t[last:] if depth == 0 else t[begin:])
    return "".join(out)


def link(m):
    target, _, label = m.group(1).partition("|")
    if target.strip().lower().startswith(SKIP_NS):
        return ""
    return label.rsplit("|", 1)[-1] if label else target


def clean(t):
    m = TAIL.search(t)
    if m:
        t = t[:m.start()]
    t = COMMENT.sub("", t)
    t = TAGBLOCK.sub("", t)
    t = strip_nested(t, TEMPLATE, "{{", listing)
    t = strip_nested(t, TABLE, "{|")
    for _ in range(3):
        t = LINK.sub(link, t)
    t = EXTLINK.sub(r"\1", t)
    t = TAG.sub("", t)
    t = HEADING.sub(r"\2", t)
    t = LISTMARK.sub("", t)
    t = QUOTES.sub("", t)
    t = MAGIC.sub("", t)
    t = SPACES.sub(" ", html.unescape(t))
    lines = (line.strip() for line in t.split("\n"))
    return "\n".join(line for line in lines if line) + "\n"


def scan(task):
    src, path, start, end, out, min_words = task
    found = []
    for pid, _, _, raw in pages(path, start, end):
        s = score(raw) if src == "wikipedia" else 0
        if src == "wikipedia" and not s:
            continue
        words = len(clean(raw.decode("utf-8", "replace")).split())
        if words >= (min_words if src == "wikipedia" else 1):
            found.append((s, words, pid))
    return found


def save(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def write(task):
    src, path, start, end, out, _ = task
    host = WIKIS[src][1]
    metas = []
    for pid, revid, title, raw in pages(path, start, end):
        if pid not in SELECTED:
            continue
        raw = raw.decode("utf-8", "replace")
        text = clean(raw)
        save(os.path.join(out, "raw", src, f"{pid}.wiki"), raw)
        save(os.path.join(out, "text", src, f"{pid}.txt"), text)
        metas.append({"id": pid, "title": title, "revid": revid,
                      "url": f"https://{host}/wiki/" + urllib.parse.quote(title.replace(" ", "_")),
                      "categories": [c.strip() for c in CATEGORY.findall(raw)],
                      "file": f"{pid}.wiki", "words": len(text.split())})
    return metas


def progress(it, total, label):
    t0 = time.time()
    for i, x in enumerate(it, 1):
        if i % max(1, total // 50) == 0 or i == total:
            print(f"{label}: {i}/{total}, {time.time() - t0:.0f} s", file=sys.stderr, flush=True)
        yield x


def finished(out, src):
    path = os.path.join(out, "raw", src, "meta.jsonl")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        return sum(1 for _ in f)


def stream_offsets(index, size):
    offs, last = [], -1
    with bz2.open(index, "rb") as f:
        for line in f:
            o = int(line[:line.index(b":")])
            if o != last:
                offs.append(o)
                last = o
    return offs + [size]


def wiki(src, dump, index, args, limit):
    global SELECTED
    offs = stream_offsets(index, os.path.getsize(dump))
    tasks, start = [], offs[0]
    for o in offs[1:]:
        if o - start >= CHUNK or o == offs[-1]:
            tasks.append((src, dump, start, o, args.out, args.min_words))
            start = o
    ctx = multiprocessing.get_context("fork")
    with ctx.Pool(args.workers) as pool:
        found = [c for part in progress(pool.imap_unordered(scan, tasks), len(tasks), f"{src} scan")
                 for c in part]
    found.sort(key=lambda c: (-c[0], -c[1], c[2]))
    print(f"{src}: {len(found)} candidates", file=sys.stderr, flush=True)
    SELECTED = frozenset(c[2] for c in (found if limit is None else found[:max(0, limit)]))
    for kind in ("raw", "text"):
        os.makedirs(os.path.join(args.out, kind, src), exist_ok=True)
    meta_path = os.path.join(args.out, "raw", src, "meta.jsonl")
    n = 0
    with ctx.Pool(args.workers) as pool, open(meta_path + ".tmp", "w", encoding="utf-8") as meta:
        for metas in progress(pool.imap(write, tasks), len(tasks), f"{src} write"):
            for m in metas:
                meta.write(json.dumps(m, ensure_ascii=False) + "\n")
            n += len(metas)
    os.replace(meta_path + ".tmp", meta_path)
    return n


def render(q, tags, answers):
    title = html.escape(q["Title"])
    parts = [f'<!DOCTYPE html>\n<html><head><meta charset="utf-8"></head><body>\n'
             f'<div class="question" data-score="{q.get("Score", 0)}"><h1>{title}</h1>\n'
             f'<div class="post-body">{q.get("Body", "")}</div>\n<ul class="tags">'
             + "".join(f"<li>{html.escape(t)}</li>" for t in tags) + "</ul></div>\n"]
    for a in answers:
        parts.append(f'<div class="answer" data-score="{a.get("Score", 0)}">'
                     f'<div class="post-body">{a.get("Body", "")}</div></div>\n')
    return "".join(parts) + "</body></html>\n"


def se_doc(item):
    out, q, answers = item
    qid = int(q["Id"])
    tags = re.findall(r"[^<>|]+", q.get("Tags", ""))
    answers.sort(key=lambda a: -int(a.get("Score", 0)))
    page = render(q, tags, answers)
    text = extract(page)
    save(os.path.join(out, "raw", "travelse", f"{qid}.html"), page)
    save(os.path.join(out, "text", "travelse", f"{qid}.txt"), text)
    return {"id": qid, "title": q["Title"], "url": f"https://{SE_HOST}/questions/{qid}",
            "categories": tags, "file": f"{qid}.html", "words": len(text.split())}


def write_docs(out, src, func, items, workers):
    for kind in ("raw", "text"):
        os.makedirs(os.path.join(out, kind, src), exist_ok=True)
    meta_path = os.path.join(out, "raw", src, "meta.jsonl")
    n = 0
    with multiprocessing.get_context("fork").Pool(workers) as pool, \
            open(meta_path + ".tmp", "w", encoding="utf-8") as meta:
        for m in progress(pool.imap(func, items, chunksize=200), len(items), f"{src} write"):
            if m:
                meta.write(json.dumps(m, ensure_ascii=False) + "\n")
                n += 1
    os.replace(meta_path + ".tmp", meta_path)
    return n


def stackexchange(archive, args):
    posts = os.path.join(os.path.dirname(archive), "travelse", "Posts.xml")
    if not os.path.exists(posts):
        import py7zr
        with py7zr.SevenZipFile(archive) as z:
            z.extract(path=os.path.dirname(posts), targets=["Posts.xml"])
    questions, answers = {}, defaultdict(list)
    for _, row in ET.iterparse(posts):
        if row.tag == "row":
            a = dict(row.attrib)
            if a.get("PostTypeId") == "1":
                questions[int(a["Id"])] = a
            elif a.get("PostTypeId") == "2":
                answers[int(a["ParentId"])].append(a)
            row.clear()
    items = [(args.out, questions[qid], answers.get(qid, [])) for qid in sorted(questions)]
    return write_docs(args.out, "travelse", se_doc, items, args.workers)


def reddit_get(sub, after, before):
    query = urllib.parse.urlencode({"subreddit": sub, "after": after, "before": before, "limit": 100,
                                    "sort": "asc", "fields": REDDIT_FIELDS})
    for attempt in range(30):
        try:
            with request(f"{REDDIT_API}?{query}") as r:
                d = json.loads(r.read())
            if d.get("data") is not None:
                return d["data"]
            err = d.get("error")
        except Exception as e:
            err = repr(e)
        print(f"reddit {sub} {after}: {err}, retry", file=sys.stderr, flush=True)
        time.sleep(min(120, 10 * (attempt + 1)))
    sys.exit(f"reddit: cannot fetch {sub} after {after}")


def reddit_slice(job):
    folder, sub, start, end = job
    path = os.path.join(folder, f"{sub}-{time.strftime('%Y-%m', time.gmtime(start))}.jsonl")
    if os.path.exists(path):
        return 0
    seen, after, kept = set(), start - 1, 0
    with open(path + ".part", "w", encoding="utf-8") as f:
        while True:
            data = reddit_get(sub, after, end)
            new = [p for p in data if p["id"] not in seen]
            for p in new:
                seen.add(p["id"])
                if p.get("selftext") not in (None, "", "[removed]", "[deleted]"):
                    f.write(json.dumps({**p, "subreddit": sub}, ensure_ascii=False) + "\n")
                    kept += 1
            if len(data) < 100 or not new:
                break
            after = data[-1]["created_utc"] - 1
    os.replace(path + ".part", path)
    return kept


def reddit_fetch(dumps, threads):
    folder = os.path.join(dumps, "reddit")
    os.makedirs(folder, exist_ok=True)
    jobs = []
    for sub in SUBREDDITS:
        for year in range(2008, time.gmtime().tm_year + 1):
            for q in range(4):
                start = calendar.timegm((year, 3 * q + 1, 1, 0, 0, 0))
                end = calendar.timegm((year + q // 3, (3 * q + 3) % 12 + 1, 1, 0, 0, 0))
                if start < time.time():
                    jobs.append((folder, sub, start, end))
    with ThreadPoolExecutor(threads) as ex:
        futures = [ex.submit(reddit_slice, job) for job in jobs]
        for i, fut in enumerate(as_completed(futures), 1):
            print(f"reddit: {i}/{len(jobs)} quarters, +{fut.result()} text posts", file=sys.stderr, flush=True)
    return folder


def md_html(md):
    paras = []
    for p in re.split(r"\n\s*\n", html.unescape(md)):
        p = MD_MARK.sub("", p).strip()
        if p:
            p = MD_LINK.sub(r'<a href="\2">\1</a>', MD_LINE.sub("", html.escape(p)))
            paras.append("<p>" + p.replace("\n", "<br>\n") + "</p>")
    return "\n".join(paras)


def reddit_doc(item):
    out, p, min_words = item
    title = html.unescape(p.get("title") or "")
    flair = p.get("link_flair_text")
    page = (f'<!DOCTYPE html>\n<html><head><meta charset="utf-8"></head><body>\n'
            f'<article class="post" data-subreddit="{p["subreddit"]}" data-score="{p.get("score", 0)}">'
            f'<h1>{html.escape(title)}</h1>\n'
            + (f'<span class="flair">{html.escape(flair)}</span>\n' if flair else "")
            + f'<div class="md">{md_html(p["selftext"])}</div></article>\n</body></html>\n')
    text = extract(page)
    words = len(text.split())
    if words < min_words:
        return None
    save(os.path.join(out, "raw", "reddit", f"{p['id']}.html"), page)
    save(os.path.join(out, "text", "reddit", f"{p['id']}.txt"), text)
    return {"id": p["id"], "title": title, "url": f"https://www.reddit.com/r/{p['subreddit']}/comments/{p['id']}/",
            "categories": [p["subreddit"]] + ([flair] if flair else []), "file": f"{p['id']}.html", "words": words}


def reddit(folder, args):
    posts = {}
    for name in sorted(os.listdir(folder)):
        if name.endswith(".jsonl"):
            with open(os.path.join(folder, name), encoding="utf-8") as f:
                for line in f:
                    p = json.loads(line)
                    posts[p["id"]] = p
    items = [(args.out, p, args.min_words) for p in sorted(posts.values(), key=lambda p: p["created_utc"])]
    return write_docs(args.out, "reddit", reddit_doc, items, args.workers)


def wiki_download(src, dumps, connections):
    return [download(u, os.path.join(dumps, os.path.basename(u)), connections)
            for u in wiki_urls(WIKIS[src][0], dumps)]


def main():
    ap = argparse.ArgumentParser(description="Корпус по туризму: Travel Stack Exchange + Reddit + Wikivoyage + "
                                             "Wikipedia, всего --total документов.")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data_1m"))
    ap.add_argument("--total", type=int, default=1_000_000)
    ap.add_argument("--min-words", type=int, default=100)
    ap.add_argument("--workers", type=int, default=os.cpu_count())
    ap.add_argument("--connections", type=int, default=2)
    ap.add_argument("--reddit-threads", type=int, default=6)
    ap.add_argument("--sources", nargs="+", default=list(SOURCES), choices=SOURCES)
    args = ap.parse_args()
    dumps = os.path.join(args.out, "dumps")
    os.makedirs(dumps, exist_ok=True)
    todo = [s for s in args.sources if finished(args.out, s) is None]
    with ThreadPoolExecutor(len(SOURCES)) as ex:
        jobs = {}
        if "travelse" in todo:
            jobs["travelse"] = ex.submit(download, SE_URL, os.path.join(dumps, "travel.stackexchange.com.7z"),
                                         args.connections)
        if "reddit" in todo:
            jobs["reddit"] = ex.submit(reddit_fetch, dumps, args.reddit_threads)
        for src in ("wikivoyage", "wikipedia"):
            if src in todo:
                jobs[src] = ex.submit(wiki_download, src, dumps, args.connections)
        files = {src: job.result() for src, job in jobs.items()}
    for src in todo:
        if src == "travelse":
            stackexchange(files[src], args)
        elif src == "reddit":
            reddit(files[src], args)
        elif src == "wikivoyage":
            wiki(src, *files[src], args, None)
        else:
            others = sum(finished(args.out, s) or 0 for s in SOURCES if s != "wikipedia")
            wiki(src, *files[src], args, args.total - others)
    counts = {src: finished(args.out, src) or 0 for src in args.sources}
    for src, n in counts.items():
        print(f"{src}\t{n}")
    print(f"total\t{sum(counts.values())}")


if __name__ == "__main__":
    main()
