.PHONY: check clean distribution

OBJECTS=validate

all: $(OBJECTS)

check: all
	./validate 

validate: validate.c ../../../include/ck_shm_allocator.h
	$(CC) $(CFLAGS) -o validate validate.c ../../../src/ck_shm_allocator.c

clean:
	rm -rf *.dSYM *.exe *~ *.o $(OBJECTS)

include ../../../build/regressions.build
CFLAGS+=$(PTHREAD_CFLAGS) -D_GNU_SOURCE
