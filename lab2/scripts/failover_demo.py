#!/usr/bin/env python3
import os
import signal
import subprocess
import sys
import threading
import time

import yaml
from pymongo import MongoClient

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
CRAWLER = os.path.join(ROOT, "crawler.py")
SERVICES = {"127.0.0.1:27017": "mongo1", "127.0.0.1:27018": "mongo2", "127.0.0.1:27019": "mongo3"}
STOP_AT, START_AT, END_AT = 30, 70, 110


def say(text):
    print(f"=== {time.strftime('%H:%M:%S')} {text}", flush=True)


def members(uri):
    with MongoClient(uri, serverSelectionTimeoutMS=30000) as c:
        return {m["name"]: m["stateStr"] for m in c.admin.command("replSetGetStatus")["members"]}


def compose(*args):
    subprocess.run(["docker", "compose", *args], cwd=ROOT, check=True, capture_output=True)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: failover_demo.py <config.yaml>")
    with open(sys.argv[1], encoding="utf-8") as f:
        db_cfg = yaml.safe_load(f)["db"]
    uri, name = db_cfg["uri"], db_cfg["name"]
    say("узлы: " + ", ".join(f"{n} {s}" for n, s in sorted(members(uri).items())))
    p = subprocess.Popen([sys.executable, CRAWLER, sys.argv[1]], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True)
    threading.Thread(target=lambda: [print("    | " + line, end="", flush=True) for line in p.stdout],
                     daemon=True).start()
    start = time.time()
    time.sleep(STOP_AT)
    primary = next(n for n, s in members(uri).items() if s == "PRIMARY")
    say(f"останавливаю основной узел {primary} (docker compose stop {SERVICES[primary]})")
    compose("stop", SERVICES[primary])
    while True:
        state = members(uri)
        new = [n for n, s in state.items() if s == "PRIMARY"]
        if new:
            say(f"выбран новый основной узел {new[0]} через {time.time() - start - STOP_AT:.0f} с; "
                "узлы: " + ", ".join(f"{n} {s}" for n, s in sorted(state.items())))
            break
        time.sleep(1)
    time.sleep(max(0, START_AT - (time.time() - start)))
    say(f"запускаю узел {primary} обратно (docker compose start {SERVICES[primary]})")
    compose("start", SERVICES[primary])
    time.sleep(max(0, END_AT - (time.time() - start)))
    say("останавливаю робота (SIGINT)")
    p.send_signal(signal.SIGINT)
    p.wait()
    time.sleep(1)
    say("узлы: " + ", ".join(f"{n} {s}" for n, s in sorted(members(uri).items())))
    counts = []
    for node in sorted(SERVICES):
        with MongoClient(node, directConnection=True, readPreference="secondaryPreferred") as d:
            counts.append(f"{node} — {d[name].documents.count_documents({})}")
    say("документов на узлах: " + ", ".join(counts))
    sys.exit(p.returncode)


if __name__ == "__main__":
    main()
