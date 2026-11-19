#!/usr/bin/env python3
import sys
import time

from pymongo import MongoClient
from pymongo.errors import OperationFailure, PyMongoError

MEMBERS = ["127.0.0.1:27017", "127.0.0.1:27018", "127.0.0.1:27019"]


def wait(fn, what, timeout=120):
    deadline = time.time() + timeout
    while True:
        try:
            result = fn()
            if result:
                return result
        except PyMongoError:
            pass
        if time.time() > deadline:
            sys.exit(f"mongo_init: timeout waiting for {what}")
        time.sleep(1)


def main():
    nodes = [MongoClient(m, directConnection=True, serverSelectionTimeoutMS=2000) for m in MEMBERS]
    for node, member in zip(nodes, MEMBERS):
        wait(lambda: node.admin.command("ping")["ok"], f"node {member}")
    first = nodes[0]
    try:
        first.admin.command("replSetGetStatus")
        print("replica set rs0 already initiated")
    except OperationFailure as e:
        if e.code != 94:
            raise
        first.admin.command("replSetInitiate", {
            "_id": "rs0", "members": [{"_id": i, "host": m} for i, m in enumerate(MEMBERS)]})
        print("replica set rs0 initiated")

    def status():
        states = {m["name"]: m["stateStr"] for m in first.admin.command("replSetGetStatus")["members"]}
        ok = list(states.values()).count("PRIMARY") == 1 and all(s in ("PRIMARY", "SECONDARY") for s in states.values())
        return states if ok else None

    states = wait(status, "PRIMARY and two SECONDARY members")
    for name, state in sorted(states.items()):
        print(f"  {name} {state}")


if __name__ == "__main__":
    main()
