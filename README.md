# Razor Engine

[![Build & Test](https://img.shields.io/badge/build-passing-brightgreen)](#)
[![Language C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](#)
[![Go Version](https://img.shields.io/badge/Go-1.24%2B-00ADD8)](#)
[![Python Version](https://img.shields.io/badge/Python-3.9%2B-yellow)](#)

**Razor Engine** is a high-performance, modular AI agent orchestration, intelligent routing, and terminal interface system designed for multi-model LLM workflows.

---

## Features

- ⚡ **Intelligent Prompt Router & Classifier**: Directs incoming requests to specialized models and roles based on embeddings and semantic categories.
- 🗄️ **High-Performance Context Cache**: In-memory LRU cache with TTL-based invalidation for ultra-fast cache hits.
- 🛠️ **Extensive Agent Tooling**: Native support for file manipulation, ripgrep search, background task execution, terminal management, git operations, and skill discovery.
- 🖥️ **Interactive Terminal UI**: Rich FTXUI-powered terminal dashboard with real-time status tables and interactive chat panels.
- 🔌 **Multi-Language Support**: Pure C++ core with C-ABI shared library, Go bindings via CGO, and Python utilities.

---

## Directory Structure

```
├── CMakeLists.txt           # Top-level CMake configuration
├── Makefile                 # Standard developer build targets
├── README.md                # Project documentation
├── model.yaml               # Model configuration and tool permissions
│
├── docs/                    # Documentation
│   ├── architecture.md      # System architecture & component overview
│   └── tools.md             # Complete tools reference and role access guide
│
├── scripts/                 # Operational and launch scripts
│   ├── autolaunch.sh        # Background daemon launcher
│   └── run_daemon.sh        # Build & launch daemon runner
│
├── router/                  # C++ Router engine & Daemon
│   ├── include/             # Router header files
│   └── src/                 # Router implementation & unit tests
│
├── orchestrator/            # Agent orchestrator & config manager
│   ├── include/             # Orchestrator headers
│   └── src/                 # Orchestrator implementation
│
├── ui/                      # FTXUI-based terminal user interface
│   ├── razor_ui.cpp         # Terminal UI components
│   └── standalone_main.cpp  # Standalone UI runner
│
├── pkg/                     # Go packages
│   └── router/              # Go CGO bindings to librazor_router_shared
│
├── cmd/                     # Go application entry points
│   └── demo/                # Go demo application
│
└── tools/                   # Developer CLI tools & testing utilities
    ├── daemon_logger.py     # Live colorized daemon log viewer
    ├── model_config_editor.py # Interactive model configuration TUI
    ├── role_validator.py    # YAML role schema and hierarchy validator
    └── test_router_socket.py # Socket integration test suite
```

---

## Quick Start

### 1. Build the Project

```bash
make build
```

### 2. Run Tests

```bash
make test
```

### 3. Start the Router Daemon

```bash
make run-daemon
```

### 4. Launch the Terminal UI

```bash
make run-ui
```

### 5. Launch the Go Demo

```bash
make demo
```

---

## Tooling & Utilities

| Tool | Description | Usage |
| :--- | :--- | :--- |
| **Daemon Logger** | Tail and colorize daemon logs in real time | `python3 tools/daemon_logger.py` |
| **Model Config Editor** | Interactive terminal UI to configure models & tools | `python3 tools/model_config_editor.py` |
| **Role Validator** | Validates role YAML schemas and hierarchy | `python3 tools/role_validator.py` |
| **Socket Tester** | Tests daemon UNIX domain socket connectivity | `python3 tools/test_router_socket.py` |

---

## Documentation

- [Architecture Overview](docs/architecture.md)
- [Tools Configuration Guide](docs/tools.md)

---

## License

MIT License. See project headers for details.
