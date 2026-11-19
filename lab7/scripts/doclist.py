#!/usr/bin/env python3
import json
import os
import sys

ORDER = ("wikivoyage", "wikipedia", "travelse", "reddit")


def sources(raw):
    found = sorted(d for d in os.listdir(raw) if os.path.isfile(os.path.join(raw, d, "meta.jsonl")))
    return [s for s in ORDER if s in found] + [s for s in found if s not in ORDER]


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: doclist.py RAW_DIR TEXT_DIR > docs.tsv")
    raw, text = sys.argv[1], sys.argv[2]
    for src in sources(raw):
        with open(os.path.join(raw, src, "meta.jsonl"), encoding="utf-8") as f:
            for line in f:
                meta = json.loads(line)
                path = os.path.join(text, src, os.path.splitext(meta["file"])[0] + ".txt")
                if not os.path.exists(path):
                    sys.exit(f"doclist: no text file {path}")
                title = " ".join(meta["title"].split())
                print(f"{path}\t{title}\t{meta['url']}")


if __name__ == "__main__":
    main()
