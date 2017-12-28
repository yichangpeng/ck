.PHONY: check clean distribution

OBJECTS=benchmark

all: $(OBJECTS)

check: all
	./benchmark 

benchmark: benchmark.c ../../../include/ck_shm_allocator.h
	$(CC) $(CFLAGS) -o benchmark benchmark.c ../../../src/ck_shm_allocator.c ../../../src/ck_ht.c ../../../src/ck_hs.c

clean:
	rm -rf *.dSYM *.exe *~ *.o $(OBJECTS)

include ../../../build/regressions.build
CFLAGS+=$(PTHREAD_CFLAGS) -D_GNU_SOURCE
