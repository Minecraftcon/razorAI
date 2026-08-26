---
name: chrome-devtools
description: Direct Chrome DevTools Protocol (CDP) browser automation, live JavaScript inspection, DOM analysis, and script injection for web apps and games. Use when inspecting browser pages, evaluating JS, automating tabs, injecting mods/ESP/scripts, or debugging network state.
---

# Chrome DevTools & Browser Automation Guide

This skill provides step-by-step instructions and a CLI helper script to automate Chrome/Chromium, inspect live JavaScript objects, evaluate expressions, and inject scripts via the Chrome DevTools Protocol (CDP).

---

## 1. Launching Chrome/Chromium with Remote Debugging

To allow DevTools control, ensure Chromium or Chrome is running with `--remote-debugging-port=9222`:

```bash
# Launch Chromium in background
/bin/chromium --remote-debugging-port=9222 --no-first-run --no-default-browser-check &
```
*(Or use `google-chrome` if installed).*

---

## 2. Using the Bundled CDP Helper (`cdp_tool.py`)

The skill includes a standalone CLI helper located at:
`assets/plugins/chrome-devtools-plugin/skills/chrome-devtools/scripts/cdp_tool.py`

### **A. List Open Tabs & Pages**
```bash
python3 assets/plugins/chrome-devtools-plugin/skills/chrome-devtools/scripts/cdp_tool.py list
```

### **B. Open a New Tab / Navigate**
```bash
# Open a new tab
python3 assets/plugins/chrome-devtools-plugin/skills/chrome-devtools/scripts/cdp_tool.py new "https://venge.io"

# Or navigate the active tab
python3 assets/plugins/chrome-devtools-plugin/skills/chrome-devtools/scripts/cdp_tool.py navigate "https://example.com"
```

### **C. Evaluate Live JavaScript (`eval`)**
```bash
# Inspect global window objects / game engine
python3 assets/plugins/chrome-devtools-plugin/skills/chrome-devtools/scripts/cdp_tool.py eval "Object.keys(window)"

# Inspect PlayCanvas or Game variables
python3 assets/plugins/chrome-devtools-plugin/skills/chrome-devtools/scripts/cdp_tool.py eval "window.pc ? Object.keys(window.pc) : 'No PlayCanvas'"

# Get document title and URL
python3 assets/plugins/chrome-devtools-plugin/skills/chrome-devtools/scripts/cdp_tool.py eval "({title: document.title, url: window.location.href})"
```

### **D. Inject a Custom Script / Mod (`inject`)**
Write your script to a `.js` file, then inject it directly into the active browser page:

```bash
# Write your custom JS logic
# Example: Injecting ESP, hooks, or event listeners
python3 assets/plugins/chrome-devtools-plugin/skills/chrome-devtools/scripts/cdp_tool.py inject /path/to/script.js
```

---

## 3. Writing Custom Python Automation Scripts

If you need continuous automation, websockets, or event listeners, create a dedicated script in your session scratch directory:

```python
import asyncio
import json
import urllib.request
import websockets

async def main():
    # 1. Get targets from HTTP endpoint
    targets = json.loads(urllib.request.urlopen("http://127.0.0.1:9222/json").read())
    page = next(t for t in targets if t.get("type") == "page")
    ws_url = page["webSocketDebuggerUrl"]

    # 2. Connect via WebSocket
    async with websockets.connect(ws_url) as ws:
        # Evaluate JavaScript
        cmd = {
            "id": 1,
            "method": "Runtime.evaluate",
            "params": {
                "expression": "console.log('Razor connected!'); document.body.style.border = '4px solid red';",
                "returnByValue": True
            }
        }
        await ws.send(json.dumps(cmd))
        resp = await ws.recv()
        print("CDP Result:", resp)

asyncio.run(main())
```

---

## 4. Troubleshooting & Rules
- **DO NOT** use `curl -X POST http://localhost:9222/devtools/page/<id>`. Target endpoints are WebSockets (`ws://`), not HTTP POST.
- **HTTP Endpoints Supported**:
  - `GET http://localhost:9222/json` (List tabs)
  - `GET http://localhost:9222/json/version` (Browser version & browser WebSocket)
  - `PUT http://localhost:9222/json/new?<url>` (Create tab)
  - `GET http://localhost:9222/json/activate/<id>` (Activate tab)
  - `GET http://localhost:9222/json/close/<id>` (Close tab)
- **Always write scripts directly using `write_file` and execute them with `run_command`.**
