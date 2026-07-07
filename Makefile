# Beast — a small language in beast mode.
#
# Targets:
#   make            release build -> build/beast
#   make debug      debug build with ASan/UBSan -> build/beast-debug
#   make fallback   tagged-union + switch-dispatch build (portability check)
#   make test       run the test suite against the release build
#   make test-all   run tests on release, sanitizer, GC-stress, and fallback
#   make bench      run the benchmark suite vs python3
#   make loc        count lines of C in src/
#   make clean

CC ?= gcc
CFLAGS := -std=c11 -Wall -Wextra -O2
DEBUG_CFLAGS := -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer
LDFLAGS := -lm

SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)

build/beast: $(SRC) $(HDR) | build
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

build/beast-debug: $(SRC) $(HDR) | build
	$(CC) $(DEBUG_CFLAGS) -o $@ $(SRC) $(LDFLAGS)

build/beast-fallback: $(SRC) $(HDR) | build
	$(CC) $(CFLAGS) -DBEAST_NO_NAN_BOXING -o $@ $(SRC) $(LDFLAGS)

build:
	mkdir -p build

.PHONY: all debug fallback test test-all bench loc clean

all: build/beast

debug: build/beast-debug

fallback: build/beast-fallback

test: build/beast
	tests/run_tests.sh build/beast

test-all: build/beast build/beast-debug build/beast-fallback
	@echo "== release =="
	tests/run_tests.sh build/beast
	@echo "== ASan/UBSan =="
	tests/run_tests.sh build/beast-debug
	@echo "== GC stress =="
	BEAST_GC_STRESS=1 tests/run_tests.sh build/beast
	@echo "== tagged-union fallback =="
	tests/run_tests.sh build/beast-fallback

bench: build/beast
	bench/run_bench.sh build/beast

loc:
	@wc -l src/*.c src/*.h | tail -1

clean:
	rm -rf build
