# Razor Engine Architecture Overview

Razor is a high-performance, multi-model AI agent routing, orchestration, and terminal UI system built in C++, Go, and Python.

```
+----------------------------------------------------------------+
|                         FTXUI Terminal UI                      |
|                  (ui/razor_ui, standalone_main)                |
+-------------------------------+--------------------------------+
                                |
                                v
+----------------------------------------------------------------+
|                       Razor Orchestrator                       |
|               (orchestrator/src/orchestrator.cpp)              |
+-------------------------------+--------------------------------+
                                |
                                v
+----------------------------------------------------------------+
|                       Razor Router Daemon                      |
|                   (UNIX Socket: ~/.razor/router.sock)          |
|  +------------------------+  +-------------------------------+ |
|  | Context Cache (LRU/TTL)|  | Embedding/Keyword Classifier  | |
|  +------------------------+  +-------------------------------+ |
|  | Task Manager           |  | File Inspector & Skill Engine | |
|  +------------------------+  +-------------------------------+ |
+----------------------------------------------------------------+
        |                                       |
        v                                       v
+-------------------------------+   +----------------------------+
|  Model Config (~/model.yaml)  |   |  Roles (~/.razor/roles/)   |
+-------------------------------+   +----------------------------+
```

---

## Key Subsystems

### 1. Router Engine (`router/`)
- **Socket Daemon (`socket_daemon.cpp`)**: Runs an asynchronous UNIX domain socket server accepting requests and routing prompts to appropriate models and roles.
- **Context Cache (`context_cache.cpp`)**: High-speed in-memory LRU cache with configurable TTL to avoid redundant model invocations.
- **Classifier (`embedding_classifier.cpp`)**: Categorizes user prompts into operational classes (`Build`, `Question/Chat`, `Small_Task`, `Debug`) using cosine similarity and heuristics.
- **Task Manager (`task_manager.cpp`)**: Spawns and manages synchronous or asynchronous processes with background job control.
- **File Inspector (`file_inspector.cpp`)**: Safe file reading, directory listing, MIME-type classification, and search.
- **Skill Manager (`skill_manager.cpp`)**: Discovers and loads modular agent skills from global and workspace roots.
- **C-Bridge (`c_router_bridge.cpp`)**: Exposes a standard C ABI (`c_router_bridge.h`) for foreign language integration.

### 2. Orchestrator & Configuration (`orchestrator/`)
- **Config Loader (`config.cpp`)**: Parses `model.yaml` and role configurations from `~/.razor/roles/` with YAML validation.
- **Process Runner (`process_runner.cpp`)**: Executes sub-processes and captures execution state.

### 3. Terminal Interface (`ui/`)
- **FTXUI TUI (`razor_ui.cpp`)**: Interactive, reactive terminal interface with split panes, status tables, and chat streams.

### 4. Language Bindings & Tools
- **Go CGO Wrapper (`pkg/router/`)**: Idiomatic Go client wrapping `librazor_router_shared.so`.
- **Python Tooling (`tools/`)**:
  - `model_config_editor.py`: Interactive TUI for model settings.
  - `role_validator.py`: Comprehensive YAML role schema validator.
  - `daemon_logger.py`: Real-time colorized daemon log streamer.
  - `test_router_socket.py`: Socket test suite.
