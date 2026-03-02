IMAGE_NAME=invasiv-builder
CONTAINER_NAME=invasiv-extract
REGISTRY=ghcr.io
# Dynamically get the lowercased repository owner
REPO_OWNER=$(shell echo $(USER) | tr '[:upper:]' '[:lower:]')
BASE_IMAGE=$(REGISTRY)/$(REPO_OWNER)/invasiv-base:latest

.PHONY: all build extract clean help run pull-base

all: build extract run

help:
	@echo "Invasiv Build System"
	@echo ""
	@echo "Targets:"
	@echo "  pull-base - Pull the pre-compiled base image from GHCR (fastest)"
	@echo "  build     - Build the Invasiv application using Docker (uses local cache if available)"
	@echo "  extract   - Extract the compiled binary from the Docker image to ./artifacts"
	@echo "  clean     - Remove build artifacts"
	@echo "  run       - Run the extracted binary (requires local libmpv2)"

pull-base:
	docker pull $(BASE_IMAGE)
	docker tag $(BASE_IMAGE) invasiv-base:latest

build:
	# Attempt to build using the local cache or pulled base image
	docker build -t $(IMAGE_NAME) --target builder .

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
