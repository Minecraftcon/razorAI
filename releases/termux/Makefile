# Detect Termux vs Standard Linux prefix
IS_TERMUX := $(shell if [ -n "$$TERMUX_VERSION" ] || [ -d "/data/data/com.termux/files/usr" ]; then echo 1; else echo 0; fi)
ifeq ($(IS_TERMUX),1)
  PREFIX ?= /data/data/com.termux/files/usr
else ifeq ($(shell [ $$(id -u) -eq 0 ] 2>/dev/null && echo 1),1)
  PREFIX ?= /usr/local
else
  PREFIX ?= $(HOME)/.local
endif

BINDIR ?= $(PREFIX)/bin
RAZOR_HOME ?= $(HOME)/.razor


.PHONY: all help build test test-router test-go test-valgrind demo clean run run-ui run-daemon package-skills install uninstall package-termux

all: build

help:
	@echo "Razor Engine Build & Install System"
	@echo ""
	@echo "Available targets:"
	@echo "  build             - Build all C++ libraries, daemons, and UI targets"
	@echo "  install           - Install 'razor' executable to $(BINDIR) and sync $(RAZOR_HOME)/skills and model.yaml"
	@echo "  uninstall         - Remove 'razor' from $(BINDIR)"
	@echo "  package-skills    - Package all active skills & plugins into assets/"
	@echo "  package-termux    - Ship full standalone source & assets to releases/termux"
	@echo "  test              - Run all C++ and Go unit tests"
	@echo "  test-router       - Run C++ router test suite"
	@echo "  test-go           - Run Go package unit tests"
	@echo "  test-valgrind     - Run memory leak detection on router test suite"
	@echo "  demo              - Run the Go demo application"
	@echo "  run               - Run the Razor orchestrator"
	@echo "  run-ui            - Launch the FTXUI terminal user interface"
	@echo "  run-daemon        - Build and run the Razor router socket daemon"
	@echo "  clean             - Remove build directories and compiled artifacts"

build:
	@mkdir -p build
	cmake -B build
	cmake --build build --parallel

install: build
	@mkdir -p $(BINDIR)
	@install -m 755 build/ui/razor_cpp_standalone $(BINDIR)/razor
	@if [ -f build/router/razor_router_daemon ]; then install -m 755 build/router/razor_router_daemon $(BINDIR)/razor_router_daemon; fi
	@mkdir -p $(RAZOR_HOME)/skills $(RAZOR_HOME)/plugins $(RAZOR_HOME)/sessions $(RAZOR_HOME)/roles
	@if [ -d assets/skills ]; then cp -rf assets/skills/* $(RAZOR_HOME)/skills/ 2>/dev/null || true; fi
	@if [ -d assets/plugins ]; then cp -rf assets/plugins/* $(RAZOR_HOME)/plugins/ 2>/dev/null || true; fi
	@if [ -f assets/skills_manifest.json ]; then cp -f assets/skills_manifest.json $(RAZOR_HOME)/skills_manifest.json 2>/dev/null || true; fi
	@if [ -f model.yaml ] && [ ! -f $(RAZOR_HOME)/model.yaml ]; then cp -f model.yaml $(RAZOR_HOME)/model.yaml; fi
	@if [ ! -f $(RAZOR_HOME)/config.yaml ] && [ -f config.yaml ]; then cp -f config.yaml $(RAZOR_HOME)/config.yaml; fi

	@echo ""
	@echo "============================================================"
	@echo "  ✓ Razor binary installed to : $(BINDIR)/razor"
	@echo "  ✓ Config location           : $(RAZOR_HOME)/model.yaml"
	@echo "  ✓ Skills deployed to        : $(RAZOR_HOME)/skills"
	@echo "  ✓ Launch with               : razor"
	@echo "============================================================"


uninstall:
	@rm -f $(BINDIR)/razor
	@echo "Removed $(BINDIR)/razor"

test: test-router test-go

test-router: build
	./build/router/test_router

test-go: build
	go test -v ./pkg/router/...

test-valgrind: build
	valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./build/router/test_router

demo: build
	go run ./cmd/demo

run: build
	./build/orchestrator/razor_orchestrator

run-ui: build
	./build/ui/razor_cpp_standalone

run-daemon: build
	./scripts/run_daemon.sh

package-skills:
	python3 scripts/package_skills.py

package-termux:
	@mkdir -p releases/termux
	@echo "Shipping code and assets to releases/termux..."
	@rsync -av --exclude='build' --exclude='.git' --exclude='releases' --exclude='bin' ./ releases/termux/
	@echo "Termux release tree prepared in releases/termux"

clean:
	rm -rf build bin
