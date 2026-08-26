.PHONY: all help build test test-router test-go test-valgrind demo clean run run-ui run-daemon package-skills

all: build

help:
	@echo "Razor Engine Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  build             - Build all C++ libraries, daemons, and UI targets"
	@echo "  package-skills    - Package all active skills & plugins into assets/"
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

clean:
	rm -rf build bin
