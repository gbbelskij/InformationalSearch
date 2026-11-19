#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
QUERY = "(castle || palace) && germany !museum"
OUT = os.path.join(ROOT, "report", "img", "search.png")


def main():
    index = sys.argv[1] if len(sys.argv) > 1 else None
    query = sys.argv[2] if len(sys.argv) > 2 else QUERY
    out = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else OUT
    profile = os.path.join(ROOT, "build", "ffprofile")
    os.makedirs(profile, exist_ok=True)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    srv = subprocess.Popen([sys.executable, os.path.join(ROOT, "web", "server.py"), "--port", "0"] +
                           (["--index", index] if index else []),
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    try:
        base = srv.stdout.readline().strip()
        url = base + "search?" + urllib.parse.urlencode({"q": query})
        subprocess.run(["firefox", "--headless", "--no-remote", "--profile", profile, "--window-size=1000,1000",
                        "--screenshot", out, url], capture_output=True, timeout=120, check=True)
        print(out)
    finally:
        srv.terminate()
        srv.wait()
        shutil.rmtree(profile, ignore_errors=True)


if __name__ == "__main__":
    main()
