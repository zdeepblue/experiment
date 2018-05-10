#
# Makefile to build and run the standalone cbt bitmap tests.
#
# To run the automatic tests:
#    make run-tests
#
# To run the benchmark:
#    make run-benchmark

CFLAGS := -g -m64 -Wall -Werror -Wno-unused-but-set-variable -DCBT_BITMAP_UNITTEST
INC_PATH := -I..

BENCHMARK_LOOPCOUNT = 1000000

test: test.c ../sparseBitmap.c
	$(CC) $(CFLAGS) $(INC_PATH) -O2 -o $@ $^

run-tests: test
	./test

run-benchmark: test
	./test perf-rand $(BENCHMARK_LOOPCOUNT)
	./test perf-user $(BENCHMARK_LOOPCOUNT)

clean:
	rm -f test
