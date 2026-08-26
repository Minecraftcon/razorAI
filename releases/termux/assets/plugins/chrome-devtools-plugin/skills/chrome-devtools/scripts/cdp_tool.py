#!/usr/bin/env python3
"""
Razor Chrome DevTools Protocol (CDP) Helper Tool
Provides direct command-line automation for Chrome/Chromium via CDP WebSockets and HTTP endpoints.
"""

import sys
import os
import json
import urllib.request
import urllib.error
import argparse
import asyncio

try:
    import websockets
except ImportError:
    # If websockets package is not installed, install or provide socket fallback
    os.system("pip install --quiet websockets 2>/dev/null || pip3 install --quiet websockets 2>/dev/null")
    try:
        import websockets
    except ImportError:
        websockets = None

CDP_PORT = int(os.environ.get("CDP_PORT", 9222))
CDP_HOST = os.environ.get("CDP_HOST", "127.0.0.1")

def http_get(path):
    url = f"http://{CDP_HOST}:{CDP_PORT}{path}"
    req = urllib.request.Request(url)
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read().decode())
    except Exception as e:
        return {"error": f"Failed to connect to {url}: {e}"}

def http_put(path):
    url = f"http://{CDP_HOST}:{CDP_PORT}{path}"
    req = urllib.request.Request(url, method="PUT")
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read().decode())
    except Exception as e:
        return {"error": f"Failed to connect to {url}: {e}"}

def get_targets():
    data = http_get("/json")
    if isinstance(data, list):
        return data
    return []

def get_active_page_ws():
    targets = get_targets()
    pages = [t for t in targets if t.get("type") == "page"]
    if pages:
        return pages[0].get("webSocketDebuggerUrl")
    return None

async def send_cdp_command(ws_url, method, params=None):
    if not ws_url:
        return {"error": "No active page WebSocket found. Ensure Chrome is running with --remote-debugging-port=9222."}
    
    if websockets is None:
        return {"error": "'websockets' Python library is required. Run 'pip install websockets'."}

    try:
        async with websockets.connect(ws_url, max_size=100_000_000, ping_interval=None) as ws:
            msg_id = 1
            payload = {
                "id": msg_id,
                "method": method,
                "params": params or {}
            }
            await ws.send(json.dumps(payload))
            
            while True:
                resp_text = await asyncio.wait_for(ws.recv(), timeout=10.0)
                resp = json.loads(resp_text)
                if resp.get("id") == msg_id:
                    return resp
    except Exception as e:
        return {"error": f"WebSocket error during {method}: {e}"}

def cmd_list(args):
    targets = get_targets()
    if not targets:
        print(f"No active targets found on {CDP_HOST}:{CDP_PORT}. Is Chrome/Chromium running with --remote-debugging-port={CDP_PORT}?")
        sys.exit(1)
    print(f"=== Active Browser Targets ({len(targets)}) ===")
    for idx, t in enumerate(targets, 1):
        print(f"[{idx}] {t.get('type', 'target').upper()}: {t.get('title', 'No Title')}")
        print(f"    URL: {t.get('url', '')}")
        print(f"    ID : {t.get('id', '')}")
        print(f"    WS : {t.get('webSocketDebuggerUrl', '')}\n")

def cmd_new(args):
    url = args.url if args.url else "about:blank"
    res = http_put(f"/json/new?{urllib.parse.quote(url, safe=':/?=&')}")
    print(json.dumps(res, indent=2))

def cmd_eval(args):
    ws_url = args.ws or get_active_page_ws()
    expr = args.expression
    params = {
        "expression": expr,
        "includeCommandLineAPI": True,
        "returnByValue": True,
        "awaitPromise": True
    }
    res = asyncio.run(send_cdp_command(ws_url, "Runtime.evaluate", params))
    if "result" in res and "result" in res["result"]:
        val = res["result"]["result"].get("value")
        if val is not None:
            if isinstance(val, (dict, list)):
                print(json.dumps(val, indent=2))
            else:
                print(val)
        else:
            desc = res["result"]["result"].get("description", str(res["result"]["result"]))
            print(desc)
    else:
        print(json.dumps(res, indent=2))

def cmd_inject(args):
    ws_url = args.ws or get_active_page_ws()
    script_path = args.file
    if not os.path.exists(script_path):
        print(f"Error: Script file not found: {script_path}")
        sys.exit(1)
    
    with open(script_path, "r", encoding="utf-8") as f:
        code = f.read()

    params = {
        "expression": code,
        "includeCommandLineAPI": True,
        "returnByValue": True,
        "awaitPromise": True
    }
    res = asyncio.run(send_cdp_command(ws_url, "Runtime.evaluate", params))
    print(f"Script injected ({len(code)} bytes). Result:")
    if "result" in res and "result" in res["result"]:
        val = res["result"]["result"].get("value")
        print(json.dumps(val, indent=2) if val is not None else res["result"]["result"])
    else:
        print(json.dumps(res, indent=2))

def cmd_navigate(args):
    ws_url = args.ws or get_active_page_ws()
    params = {"url": args.url}
    res = asyncio.run(send_cdp_command(ws_url, "Page.navigate", params))
    print(f"Navigated to: {args.url}")
    print(json.dumps(res, indent=2))

def main():
    parser = argparse.ArgumentParser(description="Razor Chrome DevTools Protocol (CDP) CLI Helper")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # list
    p_list = subparsers.add_parser("list", help="List all open browser pages and targets")
    p_list.set_defaults(func=cmd_list)

    # new
    p_new = subparsers.add_parser("new", help="Open a new tab with optional URL")
    p_new.add_argument("url", nargs="?", default="about:blank", help="URL to open")
    p_new.set_defaults(func=cmd_new)

    # eval
    p_eval = subparsers.add_parser("eval", help="Evaluate JavaScript in the active page")
    p_eval.add_argument("expression", help="JavaScript code / expression to evaluate")
    p_eval.add_argument("--ws", help="Specific target WebSocket URL")
    p_eval.set_defaults(func=cmd_eval)

    # inject
    p_inject = subparsers.add_parser("inject", help="Inject a JavaScript file into the active page")
    p_inject.add_argument("file", help="Path to JavaScript file to inject")
    p_inject.add_argument("--ws", help="Specific target WebSocket URL")
    p_inject.set_defaults(func=cmd_inject)

    # navigate
    p_nav = subparsers.add_parser("navigate", help="Navigate the active page to a URL")
    p_nav.add_argument("url", help="Target URL")
    p_nav.add_argument("--ws", help="Specific target WebSocket URL")
    p_nav.set_defaults(func=cmd_navigate)

    args = parser.parse_args()
    args.func(args)

if __name__ == "__main__":
    main()
