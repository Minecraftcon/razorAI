# Razor Tools Configuration Guide

This guide explains how to configure tools for models in Razor and provides a comprehensive specification of all available tools.

---

## 1. How Tool Access Works

By default, models do not have access to tools unless explicitly granted. The Razor router daemon collects tool permissions from **two sources**:
1. Directly from the **Model Configuration** (`model.yaml`).
2. From the **Role Configuration** (`~/.razor/roles/*.yaml`) when a model assumes that role.

If a tool is specified in either place, the model is granted access when routing a request. The tools are deduplicated and sent as part of the JSON payload to the LLM backend.

---

## 2. Configuring Tools

### Adding Tools via `model.yaml`

You can grant tool access directly to a model by adding a `tools:` list under its entry in `model.yaml`. This ensures the model *always* has access to those tools, regardless of the role it is currently assuming.

```yaml
models:
  - name: Mistral Medium
    model: mistral-medium-2508
    provider: mistral
    apiKey: ${MISTRAL_API_KEY}
    roles:
      - thinkchat
      - builder
    tools:
      - read_files
      - write_files
      - web_search
```

> **Tip:** You can use `tools/model_config_editor.py` TUI to interactively inspect and manage model configurations and tool permissions.

### Adding Tools via Roles (`~/.razor/roles/`)

You can also grant tool access dynamically based on the role the model is assigned. This allows you to restrict sensitive tools (like `run_command`) only to operational roles like `builder`.

```yaml
roleName: builder
sysPrompt:
  - You are a specialized builder agent.
executionPolicy: loop
loopbackLimit: 5
tools:
  - run_command
  - list_directory
  - read_files
  - write_files
```

---

## 3. Master List of Available Tools

Below is the master list of all tools supported by the Razor architecture. You can also specify `"all"` to grant access to every tool.

### File & Directory Operations
- `read_file` — Read a single file with line specifications (avoids loading entire large files).
- `read_files` — Read multiple files simultaneously.
- `write_file` / `write_files` — Create or overwrite files at specified paths.
- `replace_in_file` — Replace specific target content or line ranges in a file.
- `list_directory` — List contents of a directory with detailed file permissions, sizes, and timestamps.
- `find_files` — Search for files recursively within directory trees.
- `grep` — Ripgrep-powered text search across codebase files.

### Execution & Terminal Management
- `run_command` — Execute commands in shell. Outputs an ID for long-running processes if not completed within timeout.
- `manage_task` — List, inspect status, kill, or send input to running background processes.
- `term_manage` — Manage persistent terminal sessions and interactively stream stdin/stdout.

### Git Operations
- `git_status` — Inspect working directory status and branch state.
- `git_diff` — View uncommitted changes and diffs.

### Web & Network
- `web_search` — Query web search engines for external knowledge.
- `fetch_url` — Retrieve and parse static content from URLs.

### Flow Control & Interactions
- `timer` — Set a timer on running operations and await notifications.
- `sleep` — Pause execution for a specified duration.
- `notify` — Dispatch notification events.
- `ask_user` — Prompt user with interactive multiple-choice or direct feedback modals.

### Memory & Knowledge
- `remember` — Hard-save persistent context or knowledge items to memory.
- `recall` — Recover and query saved context from memory.

### Global Wildcard
- `all` — Grants access to every available tool in the system.
