#!/usr/bin/env python3
"""Tiny client for the Unreal Editor's MCP server (streamable HTTP, JSON-RPC).

  mcp.py <port> list                     -> tool names + descriptions
  mcp.py <port> schema <tool>            -> one tool's input schema
  mcp.py <port> call <tool> '<json>'     -> call a tool (args as JSON, or @file)
"""
import json
import sys
import urllib.request

PORT = sys.argv[1]
URL = "http://127.0.0.1:%s/mcp" % PORT
SESSION = None
_ID = 0


def post(method, params=None, notify=False, timeout=600):
    global SESSION, _ID
    body = {"jsonrpc": "2.0", "method": method}
    if params is not None:
        body["params"] = params
    if not notify:
        _ID += 1
        body["id"] = _ID
    headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
    if SESSION:
        headers["Mcp-Session-Id"] = SESSION
    req = urllib.request.Request(URL, data=json.dumps(body).encode(), headers=headers, method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        sid = resp.headers.get("Mcp-Session-Id")
        if sid:
            SESSION = sid
        raw = resp.read().decode("utf-8", "replace")
        ctype = resp.headers.get("Content-Type", "")
    if notify or not raw.strip():
        return None
    if "text/event-stream" in ctype:
        last = None
        for line in raw.splitlines():
            if line.startswith("data:"):
                try:
                    msg = json.loads(line[5:].strip())
                except ValueError:
                    continue
                if msg.get("id") == body.get("id"):
                    last = msg
        return last
    return json.loads(raw)


def init():
    post("initialize", {"protocolVersion": "2025-03-26", "capabilities": {},
                        "clientInfo": {"name": "mcp-cli", "version": "1"}})
    post("notifications/initialized", notify=True)


def main():
    cmd = sys.argv[2]
    init()
    if cmd == "list":
        tools = post("tools/list", {})["result"]["tools"]
        for t in tools:
            print("%-60s %s" % (t["name"], (t.get("description") or "").replace("\n", " ")[:150]))
    elif cmd == "schema":
        tools = post("tools/list", {})["result"]["tools"]
        for t in tools:
            if t["name"] == sys.argv[3]:
                print(json.dumps(t, indent=1))
    elif cmd == "call":
        arg = sys.argv[4] if len(sys.argv) > 4 else "{}"
        if arg.startswith("@"):
            arg = open(arg[1:]).read()
        res = post("tools/call", {"name": sys.argv[3], "arguments": json.loads(arg)})
        if "error" in res:
            print("ERROR", json.dumps(res["error"], indent=1))
            sys.exit(2)
        out = res["result"]
        for c in out.get("content", []):
            if c.get("type") == "text":
                print(c["text"])
            elif c.get("type") == "image":
                import base64
                path = sys.argv[5] if len(sys.argv) > 5 else "mcp_image.png"
                open(path, "wb").write(base64.b64decode(c["data"]))
                print("[image saved to %s, %s]" % (path, c.get("mimeType")))
            else:
                print(json.dumps(c)[:2000])
        if out.get("isError"):
            sys.exit(3)


if __name__ == "__main__":
    main()
