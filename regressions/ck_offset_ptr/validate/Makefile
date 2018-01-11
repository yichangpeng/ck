.PHONY: check clean distribution

OBJECTS=validate

all: $(OBJECTS)

check: all
	./validate 

validate: validate.c ../../../include/ck_offset_ptr.h
	$(CC) $(CFLAGS) -o validate validate.c

clean:
	rm -rf *.dSYM *.exe *~ *.o $(OBJECTS)

include ../../../build/regressions.build
CFLAGS+=$(PTHREAD_CFLAGS) -D_GNU_SOURCE
