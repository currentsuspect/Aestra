#!/usr/bin/env python3
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
"""Manage who can read the `early` update channel. Only hashes are stored (in the FOUNDERS KV namespace).

    founders.py add "Name"     make a key, store its hash, print the key ONCE (hand it over privately)
    founders.py revoke "Name"  that founder's key stops working at once
    founders.py list           names and dates, never keys

Needs CLOUDFLARE_EMAIL, CLOUDFLARE_API_KEY (global key), CLOUDFLARE_ACCOUNT_ID in the environment, and the namespace id
from worker/wrangler.toml.
"""
import hashlib
import json
import os
import re
import secrets
import sys
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
NAMESPACE = re.search(r'binding = "FOUNDERS"\s*\nid = "([0-9a-f]{32})"', open(os.path.join(HERE, "worker", "wrangler.toml")).read()).group(1)


def call(method, path, body=None, raw=False):
    base = f"https://api.cloudflare.com/client/v4/accounts/{os.environ['CLOUDFLARE_ACCOUNT_ID']}/storage/kv/namespaces/{NAMESPACE}"
    req = urllib.request.Request(base + path, method=method, data=body.encode() if body is not None else None)
    req.add_header("X-Auth-Email", os.environ["CLOUDFLARE_EMAIL"])
    req.add_header("X-Auth-Key", os.environ["CLOUDFLARE_API_KEY"])
    with urllib.request.urlopen(req, timeout=30) as r:
        data = r.read().decode()
    return data if raw else json.loads(data)


def sha(token):
    return hashlib.sha256(token.encode()).hexdigest()


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "add" and len(sys.argv) == 3:
        name = sys.argv[2]
        token = "aef_" + secrets.token_hex(16)
        record = json.dumps({"name": name, "created": int(time.time())})
        call("PUT", "/values/" + sha(token), record)
        # A second entry, by name, so revoke can find the hash without anyone keeping the key.
        call("PUT", "/values/name:" + urllib.parse.quote(name, safe=""), json.dumps({"hash": sha(token)}))
        print(token)
    elif cmd == "revoke" and len(sys.argv) == 3:
        name = sys.argv[2]
        by_name = json.loads(call("GET", "/values/name:" + urllib.parse.quote(name, safe=""), raw=True))
        record = json.loads(call("GET", "/values/" + by_name["hash"], raw=True))
        record["revoked"] = True
        call("PUT", "/values/" + by_name["hash"], json.dumps(record))
        print(f"revoked {name}")
    elif cmd == "list":
        for k in call("GET", "/keys?prefix=name:")["result"]:
            by_name = json.loads(call("GET", "/values/" + urllib.parse.quote(k["name"], safe=""), raw=True))
            record = json.loads(call("GET", "/values/" + by_name["hash"], raw=True))
            when = time.strftime("%Y-%m-%d", time.gmtime(record.get("created", 0)))
            print(f"{k['name'][5:]}\t{when}\t{'REVOKED' if record.get('revoked') else 'active'}")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
