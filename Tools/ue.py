#!/usr/bin/env python3
"""ue.py <toolset> <tool> ['<json args>' | @file] [image_out.png]  -> calls an Unreal MCP toolset tool on port 8002."""
import base64, json, sys, os
sys.argv.insert(1, "8002")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mcp
toolset, tool = sys.argv[2], sys.argv[3]
arg = sys.argv[4] if len(sys.argv) > 4 else "{}"
if arg.startswith("@"):
    arg = open(arg[1:]).read()
img = sys.argv[5] if len(sys.argv) > 5 else None
mcp.init()
res = mcp.post("tools/call", {"name": "call_tool", "arguments": {"toolset_name": toolset, "tool_name": tool, "arguments": json.loads(arg)}})
if "error" in res:
    print("ERROR", json.dumps(res["error"])[:3000]); sys.exit(2)
out = res["result"]
def handle_obj(o):
    rv = o.get("returnValue") if isinstance(o, dict) else None
    if isinstance(rv, dict) and "data" in rv and "mimeType" in rv:
        path = img or "ue_image.png"
        open(path, "wb").write(base64.b64decode(rv["data"]))
        print("[image %s saved: %s]" % (rv["mimeType"], path)); return True
    return False
for c in out.get("content", []):
    if c.get("type") == "text":
        t = c["text"]
        try:
            o = json.loads(t)
            if handle_obj(o): continue
            if isinstance(o, dict) and isinstance(o.get("returnValue"), str):
                t = o["returnValue"]
        except ValueError:
            pass
        print(t)
    elif c.get("type") == "image":
        path = img or "ue_image.png"
        open(path, "wb").write(base64.b64decode(c["data"]))
        print("[image %s saved: %s]" % (c.get("mimeType"), path))
    else:
        print(json.dumps(c)[:3000])
if out.get("isError"):
    sys.exit(3)
