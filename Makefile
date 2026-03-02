IMAGE_NAME=invasiv-builder
CONTAINER_NAME=invasiv-extract

.PHONY: all build extract clean help run

all: build extract run

help:
	@echo "Invasiv Build System"
	@echo ""
	@echo "Targets:"
	@echo "  build    - Build the Invasiv application using Docker"
	@echo "  extract  - Extract the compiled binary from the Docker image to ./artifacts"
	@echo "  clean    - Remove build artifacts and Docker images"
	@echo "  run      - Run the extracted binary (requires local libmpv2)"

build:
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
	docker rmi $(IMAGE_NAME) || true
