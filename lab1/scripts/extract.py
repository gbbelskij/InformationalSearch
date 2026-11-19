#!/usr/bin/env python3
"""Выделение текста из HTML статей: data/raw/<src>/*.html -> data/text/<src>/*.txt.

Удаляются теги, стили, скрипты, сноски [1], навигационные шаблоны и списки литературы.
"""
import glob
import html.parser
import os
import re

ROOT = os.path.join(os.path.dirname(__file__), "..", "data")
VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link",
        "meta", "source", "track", "wbr"}
SKIP_TAGS = {"style", "script", "sup"}
SKIP_CLASSES = {"navbox", "reflist", "mw-references-wrap", "references",
                "metadata", "mw-editsection", "noprint", "mw-empty-elt"}
BLOCK = {"p", "div", "li", "tr", "h1", "h2", "h3", "h4", "h5", "h6", "table",
         "ul", "ol", "dl", "dd", "dt", "br", "blockquote"}


class Extractor(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts, self.skip = [], 0

    def handle_starttag(self, tag, attrs):
        if tag in VOID:
            if tag == "br" and not self.skip:
                self.parts.append("\n")
            return
        classes = set((dict(attrs).get("class") or "").split())
        if self.skip or tag in SKIP_TAGS or classes & SKIP_CLASSES:
            self.skip += 1
        elif tag in BLOCK:
            self.parts.append("\n")

    def handle_endtag(self, tag):
        if tag in VOID:
            return
        if self.skip:
            self.skip -= 1
        elif tag in BLOCK:
            self.parts.append("\n")

    def handle_data(self, data):
        if not self.skip:
            self.parts.append(data)


def extract(raw):
    p = Extractor()
    p.feed(raw)
    text = "".join(p.parts)
    text = re.sub(r"[ \t ]+", " ", text)
    lines = (l.strip() for l in text.split("\n"))
    return "\n".join(l for l in lines if l) + "\n"


def main():
    for path in sorted(glob.glob(os.path.join(ROOT, "raw", "*", "*.html"))):
        src = os.path.basename(os.path.dirname(path))
        out_dir = os.path.join(ROOT, "text", src)
        os.makedirs(out_dir, exist_ok=True)
        with open(path, encoding="utf-8") as f:
            text = extract(f.read())
        name = os.path.basename(path)[:-5] + ".txt"
        with open(os.path.join(out_dir, name), "w", encoding="utf-8") as f:
            f.write(text)


if __name__ == "__main__":
    main()
