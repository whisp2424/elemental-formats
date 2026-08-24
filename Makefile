CC := gcc
CFLAGS := -std=c99 -O2 -Wall -Wextra -Wpedantic

EL01_SRC := src/common.c src/el01.c src/imgin.c src/imgout.c src/main_el01.c
IDX2_SRC := src/common.c src/idx2.c src/main_idx2.c

all: bin/el01 bin/idx2

bin/el01: $(EL01_SRC) $(wildcard src/*.h)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $(EL01_SRC) -static

bin/idx2: $(IDX2_SRC) $(wildcard src/*.h)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $(IDX2_SRC) -static

.PHONY: test clean
test: all
	python3 tests/run_tests.py

clean:
	rm -rf bin tests/out
