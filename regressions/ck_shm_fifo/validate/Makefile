.PHONY: check clean distribution

OBJECTS=ck_shm_fifo_spsc ck_shm_fifo_mpmc ck_shm_fifo_spsc_iterator ck_shm_fifo_mpmc_iterator mpmc_trypush_pop mpmc_push_pop

all: $(OBJECTS)

check: all
	./ck_shm_fifo_spsc $(CORES) 1 64000
	./ck_shm_fifo_mpmc $(CORES) 1 16000
	./ck_shm_fifo_spsc_iterator
	./ck_shm_fifo_mpmc_iterator
	./mpmc_trypush_pop 4 1 0
	./mpmc_push_pop 4 1 0

ck_shm_fifo_spsc: ck_shm_fifo_spsc.c ../../../include/ck_shm_fifo.h
	$(CC) $(CFLAGS) -o ck_shm_fifo_spsc ck_shm_fifo_spsc.c

ck_shm_fifo_mpmc: ck_shm_fifo_mpmc.c ../../../include/ck_shm_fifo.h
	$(CC) $(CFLAGS) -o ck_shm_fifo_mpmc ck_shm_fifo_mpmc.c

ck_shm_fifo_spsc_iterator: ck_shm_fifo_spsc_iterator.c ../../../include/ck_shm_fifo.h
	$(CC) $(CFLAGS) -o ck_shm_fifo_spsc_iterator ck_shm_fifo_spsc_iterator.c

ck_shm_fifo_mpmc_iterator: ck_shm_fifo_mpmc_iterator.c ../../../include/ck_shm_fifo.h
	$(CC) $(CFLAGS) -o ck_shm_fifo_mpmc_iterator ck_shm_fifo_mpmc_iterator.c

mpmc_trypush_pop mpmc_push_pop spinlock_push_pop spinlock_eb_push_pop pthreads_push_pop: enqueue_dequeue.c 
	$(CC) -DTRYMPMC $(CFLAGS) -o mpmc_trypush_pop enqueue_dequeue.c
	$(CC) -DMPMC $(CFLAGS) -o mpmc_push_pop enqueue_dequeue.c

clean:
	rm -rf *.dSYM *.exe *~ *.o $(OBJECTS)

include ../../../build/regressions.build
CFLAGS+=$(PTHREAD_CFLAGS) -D_GNU_SOURCE
