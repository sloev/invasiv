IMAGE_NAME=invasiv-builder
CONTAINER_NAME=invasiv-extract
REGISTRY=ghcr.io
# Dynamically get the repository owner from git origin
REPO_OWNER=$(shell git remote get-url origin | sed -E 's/.*[:\/](.*)\/.*/\1/' | tr '[:upper:]' '[:lower:]')
BASE_IMAGE_URL=$(REGISTRY)/$(REPO_OWNER)/invasiv-base:latest

# Check if local base exists, if so use it to skip building OF
HAS_LOCAL_BASE=$(shell docker images -q invasiv-base:latest 2>/dev/null)
ifneq ($(HAS_LOCAL_BASE),)
	BASE_ARG=--build-arg BASE_IMAGE=invasiv-base:latest
else
	BASE_ARG=
endif

.PHONY: all build test extract clean help run pull-base

all: build extract run

help:
	@echo "Invasiv Build System"
	@echo ""
	@echo "Targets:"
	@echo "  pull-base - Pull the pre-compiled base image from GHCR (fastest)"
	@echo "  build     - Build the Invasiv application using Docker (uses local cache if available)"
	@echo "  test      - Run unit and sync tests inside Docker"
	@echo "  extract   - Extract the compiled binary from the Docker image to ./artifacts"
	@echo "  clean     - Remove build artifacts"
	@echo "  run       - Run the extracted binary (requires local libmpv2)"

pull-base:
	docker pull $(BASE_IMAGE_URL)
	docker tag $(BASE_IMAGE_URL) invasiv-base:latest

build:
	@echo "Building Invasiv (Docker)... "
	docker build -t $(IMAGE_NAME) $(BASE_ARG) --target builder .

test:
	@echo "Running Tests (Docker)... "
	docker build -t invasiv-tester $(BASE_ARG) --target tester .

test-verbose:
	@echo -n "Running Tests (Docker)... "
	@docker build -t invasiv-tester $(BASE_ARG) --target tester . && echo "PASSED" || (echo "FAILED" && exit 1)

extract:
	@mkdir -p artifacts
	docker rm -f $(CONTAINER_NAME) 2>/dev/null || true
	docker create --name $(CONTAINER_NAME) $(IMAGE_NAME)
	docker cp $(CONTAINER_NAME):/of/apps/myApps/invasiv/bin/ ./artifacts/
	docker rm -f $(CONTAINER_NAME)
	@echo "Build artifacts extracted to ./artifacts/bin"

run:
	cd artifacts/bin && ./invasiv

clean:
	rm -rf artifacts
