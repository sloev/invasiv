# Stage 6: Tester - verify binary and protocol
FROM builder AS tester
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 libmpv2 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /of/apps/myApps/invasiv
RUN g++ -O3 tests/unit_tests.cpp -DTEST_MODE -o tests/unit_tests && ./tests/unit_tests
# Protocol driver integration test (now in headless mode)
RUN python3 tests/test_sync.py
