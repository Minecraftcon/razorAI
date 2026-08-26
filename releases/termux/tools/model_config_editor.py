#!/usr/bin/env python3
import os
import sys
import argparse
import yaml
import re
import curses
import urllib.request
import json
import subprocess

PROVIDERS = {
    "gemini": {
        "models": ["gemini-2.5-flash", "gemini-2.5-pro", "gemini-2.0-flash", "gemini-2.0-flash-lite", "gemini-1.5-pro"],
        "default_env": "${GEMINI_API_KEY}",
        "default_roles": ["thinkchat"]
    },
    "mistral": {
        "models": ["mistral-large-latest", "mistral-medium", "codestral-latest"],
        "default_env": "${MISTRAL_API_KEY}",
        "default_roles": ["builder"]
    },
    "cohere": {
        "models": ["command-r-plus", "command-r"],
        "default_env": "${COHERE_API_KEY}",
        "default_roles": ["thinkchat"]
    },
    "grok": {
        "models": ["grok-2", "grok-2-mini"],
        "default_env": "${GROK_API_KEY}",
        "default_roles": ["builder"]
    },
    "openai": {
        "models": ["gpt-4o", "gpt-4o-mini", "o3-mini"],
        "default_env": "${OPENAI_API_KEY}",
        "default_roles": ["debug"]
    },
    "custom": {
        "models": ["local-model", "qwen2.5-coder", "llama-3-8b"],
        "default_env": "${CUSTOM_API_KEY}",
        "default_roles": ["builder"]
    }
}

EMBEDDING_PROVIDERS = {
    "gemini": ["gemini-embedding-2", "gemini-embedding-001"],
    "cohere": ["embed-english-v3.0", "embed-multilingual-v3.0"],
    "openai": ["text-embedding-3-small", "text-embedding-3-large"],
    "local": ["nvidia/NV-Embed-v1", "gemma-2b-embedding"]
}

AVAILABLE_ROLES = ["thinkchat", "builder", "debug", "small_task", "builderflow"]
AVAILABLE_TOOLS = [
    "all", "read_file", "read_files", "write_file", "replace_in_file", 
    "list_directory", "find_files", "run_command", "term_manage", 
    "git_status", "git_diff", "web_search", "fetch_url", "timer", 
    "sleep", "remember", "recall", "notify", "ask_user"
]


def init_colors():
    if curses.has_colors():
        curses.start_color()
        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_CYAN, -1)                    # Header / Titles / Keys
        curses.init_pair(2, curses.COLOR_YELLOW, -1)                  # Accent / Index Numbers / YAML Lists
        curses.init_pair(3, curses.COLOR_GREEN, -1)                   # Success / Checked / String Values
        curses.init_pair(4, curses.COLOR_MAGENTA, -1)                 # Borders / Env Vars
        curses.init_pair(5, curses.COLOR_BLACK, curses.COLOR_CYAN)    # Active Highlighted Row
        curses.init_pair(6, curses.COLOR_RED, -1)                     # Warnings / Errors / Exit
        curses.init_pair(7, curses.COLOR_BLACK, curses.COLOR_YELLOW)  # Focused Checked Checkbox
        curses.init_pair(8, curses.COLOR_WHITE, -1)                   # Line Gutter Numbers


def safe_addstr(stdscr, y, x, text, attr=0):
    try:
        h, w = stdscr.getmaxyx()
        if y < 0 or y >= h or x < 0 or x >= w:
            return
        max_len = w - x
        if y == h - 1:
            max_len = max(0, w - x - 1)
        trimmed = text[:max_len]
        if not trimmed:
            return
        if attr:
            stdscr.addstr(y, x, trimmed, attr)
        else:
            stdscr.addstr(y, x, trimmed)
    except curses.error:
        pass


def resolve_env_var(env_str):
    if not env_str:
        return ""
    match = re.match(r"^\$\{([A-Za-z0-9_]+)\}$", env_str.strip())
    if match:
        return os.environ.get(match.group(1), "")
    return env_str


def fetch_live_models(provider, env_var_ref, fetch_embeddings=False):
    api_key = resolve_env_var(env_var_ref)
    if not api_key:
        return None

    try:
        if provider == "gemini":
            url = f"https://generativelanguage.googleapis.com/v1beta/models?key={api_key}"
            req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=3) as resp:
                data = json.loads(resp.read().decode('utf-8'))
                models = [m.get("name", "").replace("models/", "") for m in data.get("models", [])]
                if fetch_embeddings:
                    emb_models = [m for m in models if "embed" in m.lower()]
                    return emb_models if emb_models else None
                else:
                    gen_models = [m for m in models if "embed" not in m.lower()]
                    return gen_models if gen_models else None

        elif provider == "mistral":
            url = "https://api.mistral.ai/v1/models"
            req = urllib.request.Request(url, headers={"Authorization": f"Bearer {api_key}", "User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=3) as resp:
                data = json.loads(resp.read().decode('utf-8'))
                models = [m.get("id", "") for m in data.get("data", []) if m.get("id")]
                if fetch_embeddings:
                    emb_models = [m for m in models if "embed" in m.lower()]
                    return emb_models if emb_models else None
                else:
                    gen_models = [m for m in models if "embed" not in m.lower()]
                    return gen_models if gen_models else None

        elif provider == "openai":
            url = "https://api.openai.com/v1/models"
            req = urllib.request.Request(url, headers={"Authorization": f"Bearer {api_key}", "User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=3) as resp:
                data = json.loads(resp.read().decode('utf-8'))
                models = [m.get("id", "") for m in data.get("data", []) if m.get("id")]
                if fetch_embeddings:
                    emb_models = [m for m in models if "embedding" in m.lower()]
                    return emb_models if emb_models else None
                else:
                    gen_models = [m for m in models if "embedding" not in m.lower()]
                    return gen_models if gen_models else None

        elif provider == "cohere":
            url = "https://api.cohere.com/v1/models"
            req = urllib.request.Request(url, headers={"Authorization": f"Bearer {api_key}", "User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=3) as resp:
                data = json.loads(resp.read().decode('utf-8'))
                models = [m.get("name", m.get("id", "")) for m in data.get("models", [])]
                if fetch_embeddings:
                    emb_models = [m for m in models if "embed" in m.lower()]
                    return emb_models if emb_models else None
                else:
                    gen_models = [m for m in models if "embed" not in m.lower()]
                    return gen_models if gen_models else None

        elif provider == "grok":
            url = "https://api.x.ai/v1/models"
            req = urllib.request.Request(url, headers={"Authorization": f"Bearer {api_key}", "User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=3) as resp:
                data = json.loads(resp.read().decode('utf-8'))
                return [m.get("id", "") for m in data.get("data", []) if m.get("id")]

    except Exception:
        pass

    return None


def fetch_custom_live_models(base_url, env_var_ref=""):
    api_key = resolve_env_var(env_var_ref) if env_var_ref else ""
    raw_url = base_url.strip().rstrip("/")
    if not raw_url.startswith("http://") and not raw_url.startswith("https://"):
        raw_url = f"http://{raw_url}"

    candidate_urls = []
    if raw_url.endswith("/models"):
        candidate_urls.append(raw_url)
    else:
        candidate_urls.append(f"{raw_url}/models")

    if "/v1" in raw_url and not raw_url.endswith("/models"):
        base_root = raw_url.rsplit("/v1", 1)[0]
        candidate_urls.append(f"{base_root}/models")
        candidate_urls.append(f"{base_root}/api/tags")

    headers = {"User-Agent": "Mozilla/5.0"}
    if api_key:
        headers["Authorization"] = f"Bearer {api_key}"

    for target_url in candidate_urls:
        try:
            req = urllib.request.Request(target_url, headers=headers)
            with urllib.request.urlopen(req, timeout=3) as resp:
                data = json.loads(resp.read().decode('utf-8'))
                models_found = []

                if isinstance(data, dict):
                    if "data" in data and isinstance(data["data"], list):
                        for m in data["data"]:
                            if isinstance(m, dict):
                                m_id = m.get("id") or m.get("name") or m.get("model")
                                if m_id: models_found.append(str(m_id))
                    elif "models" in data and isinstance(data["models"], list):
                        for m in data["models"]:
                            if isinstance(m, dict):
                                m_id = m.get("name") or m.get("model") or m.get("id")
                                if m_id: models_found.append(str(m_id))
                            elif isinstance(m, str):
                                models_found.append(m)

                elif isinstance(data, list):
                    for m in data:
                        if isinstance(m, dict):
                            m_id = m.get("id") or m.get("name")
                            if m_id: models_found.append(str(m_id))
                        elif isinstance(m, str):
                            models_found.append(m)

                if models_found:
                    return list(dict.fromkeys(models_found))
        except Exception:
            continue

    return None


def open_real_nano(stdscr, output_path, config_data):
    try:
        with open(output_path, "w") as f:
            yaml.dump(config_data, f, sort_keys=False)
    except Exception:
        pass

    curses.def_shell_mode()
    curses.endwin()

    cmd = ["nano"]
    if os.path.exists("/usr/share/nano/yaml.nanorc"):
        cmd = ["nano", "-Y", "yaml"]
    cmd.append(output_path)

    try:
        subprocess.run(cmd)
    except FileNotFoundError:
        try:
            subprocess.run(["nano", output_path])
        except Exception:
            pass
    except Exception:
        pass

    curses.reset_shell_mode()
    stdscr.clear()
    curses.noecho()
    curses.cbreak()
    curses.curs_set(0)
    stdscr.keypad(True)
    init_colors()
    stdscr.refresh()

    if os.path.exists(output_path):
        try:
            with open(output_path, "r") as f:
                loaded = yaml.safe_load(f)
                if loaded and isinstance(loaded, dict):
                    config_data.clear()
                    config_data.update(loaded)
        except Exception:
            pass


def get_empty_config():
    return {
        "version": "1.0.0",
        "globalSysprompt": [],
        "models": [],
        "embeddings": []
    }


def validate_config(config_data):
    errors = []
    warnings = []
    statuses = []

    if "version" not in config_data or not config_data["version"]:
        errors.append("Missing or empty 'version' field.")

    if "models" not in config_data or not isinstance(config_data["models"], list) or len(config_data["models"]) == 0:
        warnings.append("No models configured under 'models'.")
    else:
        for idx, m in enumerate(config_data["models"]):
            m_name = m.get("name", f"Model #{idx+1}")
            if "name" not in m: errors.append(f"Model [{idx}] missing 'name'")
            if "model" not in m: errors.append(f"Model [{idx}] missing 'model'")
            if "provider" not in m: errors.append(f"Model [{idx}] missing 'provider'")

            if "apiKey" in m and m["apiKey"]:
                key = m["apiKey"]
                match = re.match(r"^\$\{([A-Za-z0-9_]+)\}$", key)
                if match:
                    env_var = match.group(1)
                    if env_var in os.environ:
                        statuses.append((m_name, f"Env Var '${env_var}' is SET", "OK"))
                    else:
                        warnings.append(f"Env var '${env_var}' for model '{m_name}' is NOT set in environment.")
                        statuses.append((m_name, f"Env Var '${env_var}' is MISSING", "MISSING"))
                else:
                    statuses.append((m_name, "Literal API Key specified", "OK"))

    if "embeddings" in config_data and isinstance(config_data["embeddings"], list):
        for idx, e in enumerate(config_data["embeddings"]):
            if "provider" not in e: errors.append(f"Embedding [{idx}] missing 'provider'")
            if "model" not in e: errors.append(f"Embedding [{idx}] missing 'model'")

    return errors, warnings, statuses


def select_from_list(stdscr, title, subtitle, items):
    curses.flushinp()
    selected_idx = 0
    top_idx = 0

    while True:
        stdscr.clear()
        h, w = stdscr.getmaxyx()
        max_visible = max(1, h - 7)

        if selected_idx < top_idx:
            top_idx = selected_idx
        elif selected_idx >= top_idx + max_visible:
            top_idx = selected_idx - max_visible + 1

        safe_addstr(stdscr, 1, (w - len(title)) // 2 if w > len(title) else 2, title, curses.color_pair(1) | curses.A_BOLD)
        safe_addstr(stdscr, 2, (w - len(subtitle)) // 2 if w > len(subtitle) else 2, subtitle, curses.color_pair(2))
        safe_addstr(stdscr, 3, 2, "─" * (w - 4), curses.color_pair(4))

        start_row = 4
        visible_count = min(max_visible, len(items) - top_idx)

        if top_idx > 0:
            safe_addstr(stdscr, start_row, 4, f"^ ({top_idx} items above)", curses.color_pair(2) | curses.A_BOLD)
            start_row += 1
            visible_count = min(max_visible - 1, len(items) - top_idx)

        for i in range(visible_count):
            item_idx = top_idx + i
            if item_idx >= len(items):
                break
            item = items[item_idx]
            row = start_row + i

            if item_idx == selected_idx:
                line_str = f"> [{item_idx+1}] {item}"
                safe_addstr(stdscr, row, 4, line_str.ljust(w - 8), curses.color_pair(5) | curses.A_BOLD)
            else:
                safe_addstr(stdscr, row, 4, "  [")
                safe_addstr(stdscr, row, 7, f"{item_idx+1}", curses.color_pair(2))
                safe_addstr(stdscr, row, 7 + len(str(item_idx+1)), f"] {item}")

        bottom_idx = top_idx + visible_count
        if bottom_idx < len(items):
            safe_addstr(stdscr, start_row + visible_count, 4, f"v ({len(items) - bottom_idx} items below)", curses.color_pair(2) | curses.A_BOLD)

        safe_addstr(stdscr, h - 2, 2, f"Item {selected_idx+1} of {len(items)} | Up/Down: navigate | Enter: select | Esc: cancel", curses.color_pair(2))
        stdscr.refresh()

        ch = stdscr.getch()
        if ch in (27, ord('q'), ord('Q')):
            return None
        elif ch in (curses.KEY_UP, ord('k')):
            selected_idx = (selected_idx - 1) % len(items)
        elif ch in (curses.KEY_DOWN, ord('j')):
            selected_idx = (selected_idx + 1) % len(items)
        elif ch in (curses.KEY_ENTER, 10, 13):
            return items[selected_idx]


def select_multiple_roles(stdscr, title, subtitle, items, default_selected=None):
    curses.flushinp()
    selected_idx = 0
    top_idx = 0
    checked = set()
    if default_selected:
        for r in default_selected:
            if r in items:
                checked.add(items.index(r))
    if not checked and items:
        checked.add(0)

    while True:
        stdscr.clear()
        h, w = stdscr.getmaxyx()
        max_visible = max(1, h - 7)

        if selected_idx < top_idx:
            top_idx = selected_idx
        elif selected_idx >= top_idx + max_visible:
            top_idx = selected_idx - max_visible + 1

        safe_addstr(stdscr, 1, (w - len(title)) // 2 if w > len(title) else 2, title, curses.color_pair(1) | curses.A_BOLD)
        safe_addstr(stdscr, 2, (w - len(subtitle)) // 2 if w > len(subtitle) else 2, subtitle, curses.color_pair(2))
        safe_addstr(stdscr, 3, 2, "─" * (w - 4), curses.color_pair(4))

        start_row = 4
        visible_count = min(max_visible, len(items) - top_idx)

        if top_idx > 0:
            safe_addstr(stdscr, start_row, 4, f"^ ({top_idx} items above)", curses.color_pair(2) | curses.A_BOLD)
            start_row += 1
            visible_count = min(max_visible - 1, len(items) - top_idx)

        for i in range(visible_count):
            item_idx = top_idx + i
            if item_idx >= len(items):
                break
            item = items[item_idx]
            row = start_row + i

            is_checked = item_idx in checked

            if item_idx == selected_idx:
                mark = "[X]" if is_checked else "[ ]"
                line_str = f"> {mark} {item}"
                safe_addstr(stdscr, row, 4, line_str.ljust(w - 8), curses.color_pair(5) | curses.A_BOLD)
            else:
                if is_checked:
                    safe_addstr(stdscr, row, 4, f"  [X] {item}", curses.color_pair(3) | curses.A_BOLD)
                else:
                    safe_addstr(stdscr, row, 4, f"  [ ] {item}")

        bottom_idx = top_idx + visible_count
        if bottom_idx < len(items):
            safe_addstr(stdscr, start_row + visible_count, 4, f"v ({len(items) - bottom_idx} items below)", curses.color_pair(2) | curses.A_BOLD)

        safe_addstr(stdscr, h - 2, 2, f"Item {selected_idx+1} of {len(items)} | Up/Down: navigate | Space: toggle | Enter: confirm | Esc: cancel", curses.color_pair(2))
        stdscr.refresh()

        ch = stdscr.getch()
        if ch in (27, ord('q'), ord('Q')):
            return None
        elif ch in (curses.KEY_UP, ord('k')):
            selected_idx = (selected_idx - 1) % len(items)
        elif ch in (curses.KEY_DOWN, ord('j')):
            selected_idx = (selected_idx + 1) % len(items)
        elif ch == ord(' '):
            if selected_idx in checked:
                checked.remove(selected_idx)
            else:
                checked.add(selected_idx)
        elif ch in (curses.KEY_ENTER, 10, 13):
            if not checked:
                checked.add(selected_idx)
            return [items[i] for i in sorted(list(checked))]


def prompt_string(stdscr, title, prompt_text, default_val=""):
    curses.flushinp()
    curses.echo()
    curses.nocbreak()
    curses.curs_set(1)
    stdscr.clear()

    safe_addstr(stdscr, 1, 2, title, curses.color_pair(1) | curses.A_BOLD)
    safe_addstr(stdscr, 3, 2, prompt_text, curses.color_pair(2))
    if default_val:
        safe_addstr(stdscr, 4, 2, f"Default: {default_val}", curses.color_pair(2))
    safe_addstr(stdscr, 6, 2, "> ", curses.color_pair(1) | curses.A_BOLD)
    stdscr.refresh()

    try:
        input_bytes = stdscr.getstr(6, 4, 100)
        result = input_bytes.decode('utf-8').strip()
    except Exception:
        result = ""

    curses.noecho()
    curses.cbreak()
    curses.curs_set(0)

    if not result and default_val:
        return default_val
    return result


def draw_yaml_line(stdscr, row, col, line, is_selected, max_width):
    bg_attr = curses.color_pair(5) if is_selected else 0

    if line.strip().startswith("#"):
        attr = (bg_attr | curses.A_DIM) if is_selected else curses.A_DIM
        safe_addstr(stdscr, row, col, line.ljust(max_width), attr)
        return

    colon_idx = line.find(":")
    if colon_idx != -1:
        key_part = line[:colon_idx+1]
        val_part = line[colon_idx+1:]

        key_attr = (bg_attr | curses.color_pair(1) | curses.A_BOLD) if is_selected else (curses.color_pair(1) | curses.A_BOLD)
        safe_addstr(stdscr, row, col, key_part, key_attr)

        rem_width = max_width - len(key_part)
        if rem_width > 0:
            val_trimmed = val_part.strip()
            if val_trimmed.startswith("${"):
                val_attr = (bg_attr | curses.color_pair(4) | curses.A_BOLD) if is_selected else (curses.color_pair(4) | curses.A_BOLD)
            else:
                val_attr = (bg_attr | curses.color_pair(3) | curses.A_BOLD) if is_selected else curses.color_pair(3)
            safe_addstr(stdscr, row, col + len(key_part), val_part.ljust(rem_width), val_attr)
    else:
        if line.strip().startswith("-"):
            dash_idx = line.find("-")
            pre_dash = line[:dash_idx]
            if pre_dash:
                safe_addstr(stdscr, row, col, pre_dash, bg_attr)

            dash_attr = (bg_attr | curses.color_pair(2) | curses.A_BOLD) if is_selected else (curses.color_pair(2) | curses.A_BOLD)
            safe_addstr(stdscr, row, col + dash_idx, "-", dash_attr)

            val_attr = (bg_attr | curses.color_pair(3) | curses.A_BOLD) if is_selected else curses.color_pair(3)
            rem_width = max_width - (dash_idx + 1)
            if rem_width > 0:
                safe_addstr(stdscr, row, col + dash_idx + 1, line[dash_idx+1:].ljust(rem_width), val_attr)
        else:
            txt_attr = (bg_attr | curses.A_BOLD) if is_selected else 0
            safe_addstr(stdscr, row, col, line.ljust(max_width), txt_attr)


def show_nano_editor(stdscr, config_data, output_path):
    curses.flushinp()
    yaml_str = yaml.dump(config_data, sort_keys=False)
    lines = yaml_str.splitlines()
    if not lines:
        lines = ["version: 1.0.0", "globalSysprompt: []", "models: []", "embeddings: []"]

    selected_line = 0
    top_line = 0

    while True:
        stdscr.clear()
        h, w = stdscr.getmaxyx()
        max_visible = max(1, h - 5)

        if selected_line < top_line:
            top_line = selected_line
        elif selected_line >= top_line + max_visible:
            top_line = selected_line - max_visible + 1

        # Nano Top Header Bar
        header_text = f"  GNU nano 8.4              {output_path}                            "
        safe_addstr(stdscr, 0, 0, header_text.ljust(w), curses.A_REVERSE | curses.A_BOLD)

        # Editor Body with Line Gutter & Multi-Color YAML Syntax
        for i in range(min(max_visible, len(lines) - top_line)):
            line_idx = top_line + i
            row = 1 + i
            line_content = lines[line_idx]

            if line_idx == selected_line:
                gutter = f"> {line_idx+1:2d} | "
                safe_addstr(stdscr, row, 0, gutter, curses.color_pair(2) | curses.A_BOLD)
            else:
                gutter = f"  {line_idx+1:2d} | "
                safe_addstr(stdscr, row, 0, gutter, curses.A_DIM)

            draw_yaml_line(stdscr, row, len(gutter), line_content, (line_idx == selected_line), w - len(gutter))

        # Nano Bottom Status Footer Bar
        footer_top = h - 3
        safe_addstr(stdscr, footer_top, 2, f"[ Line {selected_line+1}/{len(lines)} | UTF-8 | YAML Syntax Highlighted ]", curses.color_pair(2) | curses.A_BOLD)

        nano_bar_1 = "^G Get Help   ^O WriteOut   ^W Where Is   ^K Cut Text   ^J Justify   ^C Cur Pos"
        nano_bar_2 = "^X Exit       ^E Edit Line  ^R Read File  ^\\ Replace   ^U Paste     ^T To Spell"

        safe_addstr(stdscr, h - 2, 0, nano_bar_1.ljust(w), curses.A_REVERSE)
        safe_addstr(stdscr, h - 1, 0, nano_bar_2.ljust(w), curses.A_REVERSE)

        stdscr.refresh()

        ch = stdscr.getch()
        if ch in (27, ord('q'), ord('Q'), 24): # ESC, q, Ctrl+X
            break
        elif ch in (curses.KEY_UP, ord('k')):
            if selected_line > 0:
                selected_line -= 1
        elif ch in (curses.KEY_DOWN, ord('j')):
            if selected_line < len(lines) - 1:
                selected_line += 1
        elif ch in (curses.KEY_ENTER, 10, 13, ord('e'), 5): # Enter, e, Ctrl+E to edit line
            new_text = prompt_string(stdscr, f"Edit Line {selected_line+1}", f"Original: {lines[selected_line]}", lines[selected_line])
            if new_text:
                lines[selected_line] = new_text
                try:
                    updated_yaml = "\n".join(lines)
                    parsed = yaml.safe_load(updated_yaml)
                    if parsed and isinstance(parsed, dict):
                        config_data.clear()
                        config_data.update(parsed)
                except Exception:
                    pass
        elif ch in (15, ord('s'), ord('w')): # Ctrl+O or s to WriteOut / Save
            with open(output_path, "w") as f:
                f.write("\n".join(lines))


def draw_menu(stdscr, selected_idx, menu_items, status_msg=""):
    stdscr.clear()
    h, w = stdscr.getmaxyx()

    title = "RazorAI Interactive Model Configurator"
    subtitle = "Use Up/Down Arrow keys to navigate, Enter to select, 'q' to exit"

    safe_addstr(stdscr, 1, (w - len(title)) // 2 if w > len(title) else 2, title, curses.color_pair(1) | curses.A_BOLD)
    safe_addstr(stdscr, 2, (w - len(subtitle)) // 2 if w > len(subtitle) else 2, subtitle, curses.color_pair(2))
    safe_addstr(stdscr, 3, 2, "─" * (w - 4), curses.color_pair(4))

    start_row = 5
    for i, item in enumerate(menu_items):
        row = start_row + i
        if row >= h - 4:
            break

        if i == selected_idx:
            line_str = f"> [{i+1}] {item}"
            safe_addstr(stdscr, row, 4, line_str.ljust(w - 8), curses.color_pair(5) | curses.A_BOLD)
        else:
            if i == len(menu_items) - 1: # Exit option in Red accent
                safe_addstr(stdscr, row, 4, "  [")
                safe_addstr(stdscr, row, 7, f"{i+1}", curses.color_pair(6) | curses.A_BOLD)
                safe_addstr(stdscr, row, 8, f"] {item}")
            else:
                safe_addstr(stdscr, row, 4, "  [")
                safe_addstr(stdscr, row, 7, f"{i+1}", curses.color_pair(2))
                safe_addstr(stdscr, row, 8, f"] {item}")

    safe_addstr(stdscr, h - 3, 2, "─" * (w - 4), curses.color_pair(4))

    if status_msg:
        color = curses.color_pair(3) if "Error" not in status_msg and "Cannot" not in status_msg else curses.color_pair(6)
        safe_addstr(stdscr, h - 2, 2, status_msg, color)

    stdscr.refresh()


def show_scrollable_text(stdscr, title, text_lines):
    top_line = 0
    h, w = stdscr.getmaxyx()
    max_visible = h - 5

    while True:
        stdscr.clear()
        safe_addstr(stdscr, 1, 2, f"--- {title} --- (Press ESC or 'q' to return)", curses.color_pair(1) | curses.A_BOLD)
        safe_addstr(stdscr, 2, 2, "─" * (w - 4), curses.color_pair(4))

        for i in range(max_visible):
            idx = top_line + i
            if idx >= len(text_lines):
                break
            line = text_lines[idx]
            if "[ERROR]" in line or "MISSING" in line:
                safe_addstr(stdscr, 3 + i, 2, line, curses.color_pair(6))
            elif "[WARNING]" in line:
                safe_addstr(stdscr, 3 + i, 2, line, curses.color_pair(2))
            elif "100% Valid" in line or "[OK]" in line:
                safe_addstr(stdscr, 3 + i, 2, line, curses.color_pair(3) | curses.A_BOLD)
            else:
                safe_addstr(stdscr, 3 + i, 2, line)

        safe_addstr(stdscr, h - 2, 2, f"Lines {top_line+1}-{min(top_line+max_visible, len(text_lines))} of {len(text_lines)} | Up/Down to scroll | Esc/q to back", curses.color_pair(2))
        stdscr.refresh()

        ch = stdscr.getch()
        if ch in (27, ord('q'), ord('Q')):
            break
        elif ch in (curses.KEY_UP, ord('k')):
            if top_line > 0:
                top_line -= 1
        elif ch in (curses.KEY_DOWN, ord('j')):
            if top_line + max_visible < len(text_lines):
                top_line += 1


def curses_main(stdscr, output_path):
    init_colors()
    curses.curs_set(0)
    stdscr.keypad(True)

    config = None
    status_msg = ""

    if os.path.exists(output_path):
        try:
            with open(output_path, "r") as f:
                loaded = yaml.safe_load(f)
                if loaded and isinstance(loaded, dict):
                    config = loaded
                    status_msg = f"Loaded existing configuration from '{output_path}'"
        except Exception as e:
            status_msg = f"Error reading '{output_path}': {e}"

    if config is None:
        config = get_empty_config()
        status_msg = "Initialized fresh configuration (no existing file)"

    menu_items = [
        "Open in Real System Nano Editor",
        "View/Edit in Built-in Nano Mode",
        "Add Model Entry",
        "Add Embedding Entry",
        "Edit Global System Prompts",
        "Validate Configuration & Env Vars",
        "Save to model.yaml & Exit",
        "Exit without Saving"
    ]

    selected_idx = 0

    while True:
        draw_menu(stdscr, selected_idx, menu_items, status_msg)
        ch = stdscr.getch()

        if ch in (curses.KEY_UP, ord('k')):
            selected_idx = (selected_idx - 1) % len(menu_items)
        elif ch in (curses.KEY_DOWN, ord('j')):
            selected_idx = (selected_idx + 1) % len(menu_items)
        elif ch in (ord('1'), ord('2'), ord('3'), ord('4'), ord('5'), ord('6'), ord('7'), ord('8')):
            selected_idx = ch - ord('1')
        elif ch in (curses.KEY_ENTER, 10, 13):
            if selected_idx == 0:
                open_real_nano(stdscr, output_path, config)
                status_msg = f"Returned from system Nano editor ('{output_path}')"

            elif selected_idx == 1:
                show_nano_editor(stdscr, config, output_path)

            elif selected_idx == 2:
                prov = select_from_list(stdscr, "Select Model Provider", "Use Up/Down Arrows to select provider, Enter to confirm", list(PROVIDERS.keys()))
                if not prov:
                    continue

                prov_info = PROVIDERS[prov]

                if prov == "custom":
                    custom_endpoint = prompt_string(
                        stdscr,
                        "Custom Endpoint Base URL",
                        "Enter OpenAI-compatible v1 base URL (e.g. http://127.0.0.1:8080/v1 or http://localhost:11434)",
                        "http://127.0.0.1:8080/v1"
                    )
                    if not custom_endpoint:
                        custom_endpoint = "http://127.0.0.1:8080/v1"

                    api_key = prompt_string(
                        stdscr,
                        "API Key Variable",
                        "Enter Environment Variable reference or literal key (optional)",
                        prov_info["default_env"]
                    )

                    stdscr.clear()
                    h, w = stdscr.getmaxyx()
                    safe_addstr(stdscr, 3, 2, f"Fetching live models from {custom_endpoint}...", curses.color_pair(1) | curses.A_BOLD)
                    safe_addstr(stdscr, 5, 2, "Please wait a moment...", curses.color_pair(2))
                    stdscr.refresh()

                    live_models = fetch_custom_live_models(custom_endpoint, api_key)
                    curses.flushinp()

                    if live_models:
                        subtitle = f"Live models fetched from {custom_endpoint}"
                        model_options = live_models + ["Custom Model ID..."]
                    else:
                        subtitle = f"Could not reach {custom_endpoint} - Choose/Enter Model ID"
                        model_options = prov_info["models"] + ["Custom Model ID..."]

                    model_choice = select_from_list(stdscr, "Select Model ID for Custom Endpoint", subtitle, model_options)
                    if not model_choice:
                        continue

                    if model_choice == "Custom Model ID...":
                        model_id = prompt_string(stdscr, "Custom Model ID", "Enter exact model ID", "qwen2.5-coder")
                    else:
                        model_id = model_choice

                    display_name = prompt_string(stdscr, "Display Name", "Enter descriptive name for this model configuration", f"Custom-{model_id.replace('/', '-')}")

                    roles = select_multiple_roles(
                        stdscr,
                        "Assigned Roles",
                        "Up/Down: Navigate | Space: Toggle [X]/[ ] | Enter: Confirm",
                        AVAILABLE_ROLES,
                        prov_info["default_roles"]
                    )
                    if not roles:
                        roles = prov_info["default_roles"]

                    tools_selected = select_multiple_roles(
                        stdscr,
                        "Assigned Tools",
                        "Up/Down: Navigate | Space: Toggle [X]/[ ] | Enter: Confirm",
                        AVAILABLE_TOOLS,
                        ["all"]
                    )
                    if not tools_selected:
                        tools_selected = ["all"]

                    model_entry = {
                        "name": display_name,
                        "model": model_id,
                        "provider": "custom",
                        "apiKey": api_key,
                        "endpoint": custom_endpoint,
                        "roles": roles,
                        "tools": tools_selected
                    }

                else:
                    stdscr.clear()
                    h, w = stdscr.getmaxyx()
                    safe_addstr(stdscr, 3, (w - 45) // 2 if w > 45 else 2, f"Fetching live models for {prov.upper()} from API...", curses.color_pair(1) | curses.A_BOLD)
                    safe_addstr(stdscr, 5, (w - 24) // 2 if w > 24 else 2, "Please wait a moment...", curses.color_pair(2))
                    stdscr.refresh()

                    live_models = fetch_live_models(prov, prov_info["default_env"], fetch_embeddings=False)
                    curses.flushinp()

                    if live_models:
                        subtitle = f"Live models fetched from {prov.upper()} API"
                        model_options = live_models + ["Custom Model ID..."]
                    else:
                        subtitle = f"Choose model ID (Fallback mode - no live API key set)"
                        model_options = prov_info["models"] + ["Custom Model ID..."]

                    model_choice = select_from_list(stdscr, f"Select Model ID for {prov.capitalize()}", subtitle, model_options)
                    if not model_choice:
                        continue

                    if model_choice == "Custom Model ID...":
                        model_id = prompt_string(stdscr, "Custom Model ID", "Enter exact model ID", "gemini-2.5-flash")
                    else:
                        model_id = model_choice

                    display_name = prompt_string(stdscr, "Display Name", "Enter descriptive name for this model configuration", f"{prov.capitalize()}-{model_id.replace('/', '-')}")
                    api_key = prompt_string(stdscr, "API Key Variable", "Enter Environment Variable reference or literal key", prov_info["default_env"])

                    roles = select_multiple_roles(
                        stdscr,
                        "Assigned Roles",
                        "Up/Down: Navigate | Space: Toggle [X]/[ ] | Enter: Confirm",
                        AVAILABLE_ROLES,
                        prov_info["default_roles"]
                    )
                    if not roles:
                        roles = prov_info["default_roles"]

                    tools_selected = select_multiple_roles(
                        stdscr,
                        "Assigned Tools",
                        "Up/Down: Navigate | Space: Toggle [X]/[ ] | Enter: Confirm",
                        AVAILABLE_TOOLS,
                        ["all"]
                    )
                    if not tools_selected:
                        tools_selected = ["all"]

                    model_entry = {
                        "name": display_name,
                        "model": model_id,
                        "provider": prov,
                        "apiKey": api_key,
                        "roles": roles,
                        "tools": tools_selected
                    }

                if "models" not in config:
                    config["models"] = []
                config["models"].append(model_entry)
                status_msg = f"Added model '{display_name}' ({prov}/{model_id})"

            elif selected_idx == 3:
                prov = select_from_list(stdscr, "Select Embedding Provider", "Use Up/Down Arrows to select embedding provider", list(EMBEDDING_PROVIDERS.keys()))
                if not prov:
                    continue

                default_env = f"${{{prov.upper()}_API_KEY}}" if prov != "local" else ""

                stdscr.clear()
                h, w = stdscr.getmaxyx()
                safe_addstr(stdscr, 3, (w - 55) // 2 if w > 55 else 2, f"Fetching live embedding models for {prov.upper()} from API...", curses.color_pair(1) | curses.A_BOLD)
                safe_addstr(stdscr, 5, (w - 24) // 2 if w > 24 else 2, "Please wait a moment...", curses.color_pair(2))
                stdscr.refresh()

                live_emb_models = fetch_live_models(prov, default_env, fetch_embeddings=True)
                curses.flushinp()

                if live_emb_models:
                    subtitle = f"Live embedding models fetched from {prov.upper()} API"
                    models_avail = live_emb_models + ["Custom Embedding Model..."]
                else:
                    subtitle = "Choose embedding model ID"
                    models_avail = EMBEDDING_PROVIDERS[prov] + ["Custom Embedding Model..."]

                model_choice = select_from_list(stdscr, f"Select Embedding Model for {prov.capitalize()}", subtitle, models_avail)
                if not model_choice:
                    continue

                if model_choice == "Custom Embedding Model...":
                    model_id = prompt_string(stdscr, "Custom Embedding Model ID", "Enter embedding model ID", "gemini-embedding-2")
                else:
                    model_id = model_choice

                api_key = prompt_string(stdscr, "API Key Variable", "Enter Env Var", default_env)

                emb_entry = {
                    "provider": prov,
                    "model": model_id
                }
                if api_key:
                    emb_entry["apiKey"] = api_key

                if "embeddings" not in config:
                    config["embeddings"] = []
                config["embeddings"].append(emb_entry)
                status_msg = f"Added embedding provider '{prov}' ({model_id})"

            elif selected_idx == 4:
                prompts = config.get("globalSysprompt", [])
                lines = [f"System Prompt Line {i+1}: {p}" for i, p in enumerate(prompts)]
                if not lines:
                    lines = ["No global system prompts configured yet."]

                new_line = prompt_string(stdscr, "Edit System Prompts", "Enter new prompt line to add (leave blank to skip)", "")
                if new_line:
                    if "globalSysprompt" not in config:
                        config["globalSysprompt"] = []
                    config["globalSysprompt"].append(new_line)
                    status_msg = "Added global system prompt line"

            elif selected_idx == 5:
                errors, warnings, statuses = validate_config(config)
                report_lines = []

                if not errors and not warnings:
                    report_lines.append("STATUS: Schema is 100% Valid!")
                else:
                    for e in errors:
                        report_lines.append(f"[ERROR] {e}")
                    for w in warnings:
                        report_lines.append(f"[WARNING] {w}")

                report_lines.append("")
                report_lines.append("--- Environment Variables Check ---")
                for name, msg, st in statuses:
                    report_lines.append(f"{name}: {msg} [{st}]")

                show_scrollable_text(stdscr, "Validation & Environment Report", report_lines)

            elif selected_idx == 6:
                errors, warnings, _ = validate_config(config)
                if errors:
                    status_msg = f"Cannot save: {errors[0]}"
                    continue
                with open(output_path, "w") as f:
                    yaml.dump(config, f, sort_keys=False)
                break

            elif selected_idx == 7:
                break

        elif ch in (27, ord('q'), ord('Q')):
            break


def cleanup_terminal():
    try:
        curses.nocbreak()
        curses.echo()
        curses.curs_set(1)
        curses.endwin()
    except Exception:
        pass
    os.system("stty sane 2>/dev/null; printf '\\033[?25h\\033[?1049l'")


def main():
    parser = argparse.ArgumentParser(description="RazorAI Terminal Curses Config Editor")
    parser.add_argument("--validate", "-v", nargs="?", const="model.yaml", help="Validate model.yaml file")
    parser.add_argument("--output", "-o", default="model.yaml", help="Output YAML file path")

    args = parser.parse_args()

    if args.validate:
        file_path = args.validate
        if not os.path.exists(file_path):
            print(f"Error: File '{file_path}' not found.")
            sys.exit(1)
        with open(file_path, "r") as f:
            data = yaml.safe_load(f)
        errors, warnings, statuses = validate_config(data)
        print(f"=== Validation Report for {file_path} ===")
        if not errors and not warnings:
            print("STATUS: Schema is 100% Valid!")
        else:
            for e in errors: print(f"ERROR: {e}")
            for w in warnings: print(f"WARNING: {w}")
        for name, msg, st in statuses:
            print(f"{name}: {msg}")
        sys.exit(0 if not errors else 1)

    try:
        curses.wrapper(curses_main, args.output)
    except KeyboardInterrupt:
        pass
    finally:
        cleanup_terminal()
        print("Exited cleanly.")


if __name__ == "__main__":
    main()
