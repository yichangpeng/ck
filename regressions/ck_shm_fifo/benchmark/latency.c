/*
 * Copyright 2011-2015 Samy Al Bahra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ck_shm_fifo.h>
#include <ck_spinlock.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "../../common.h"

#ifndef ENTRIES
#define ENTRIES 2 
#endif

#ifndef STEPS
#define STEPS 1
#endif

#if defined(CK_F_SHM_FIFO_MPMC)
struct entry{
    ck_shm_fifo_mpmc_entry_t fifo_entry;
    int value;
};
#endif

int
main(void)
{
	ck_spinlock_fas_t mutex = CK_SPINLOCK_FAS_INITIALIZER;
	void *r;
	uint64_t s, e, a;
	unsigned int i;
	unsigned int j;

#if   defined(CK_F_SHM_FIFO_SPSC)
	ck_shm_fifo_spsc_t spsc_fifo;
	ck_shm_fifo_spsc_entry_t spsc_entry[ENTRIES];
	ck_shm_fifo_spsc_entry_t spsc_stub;
#endif

#if defined(CK_F_SHM_FIFO_MPMC)

	ck_shm_fifo_mpmc_t mpmc_fifo;
	struct entry mpmc_entry[ENTRIES];
	ck_shm_fifo_mpmc_entry_t mpmc_stub;
	ck_shm_fifo_mpmc_entry_t *garbage;
#endif

#ifdef CK_F_SHM_FIFO_SPSC
	a = 0;
	for (i = 0; i < STEPS; i++) {
		ck_shm_fifo_spsc_init(&spsc_fifo, &spsc_stub);

		s = rdtsc();
		for (j = 0; j < ENTRIES; j++) {
			ck_spinlock_fas_lock(&mutex);
			ck_shm_fifo_spsc_enqueue(&spsc_fifo, spsc_entry + j, NULL);
			ck_spinlock_fas_unlock(&mutex);
		}
		e = rdtsc();

		a += e - s;
	}
	printf("    spinlock_enqueue: %16" PRIu64 "\n", a / STEPS / (sizeof(spsc_entry) / sizeof(*spsc_entry)));

	a = 0;
	for (i = 0; i < STEPS; i++) {
		ck_shm_fifo_spsc_init(&spsc_fifo, &spsc_stub);
		for (j = 0; j < ENTRIES; j++)
			ck_shm_fifo_spsc_enqueue(&spsc_fifo, spsc_entry + j, NULL);

		s = rdtsc();
		for (j = 0; j < ENTRIES; j++) {
			ck_spinlock_fas_lock(&mutex);
			ck_shm_fifo_spsc_dequeue(&spsc_fifo, &r);
			ck_spinlock_fas_unlock(&mutex);
		}
		e = rdtsc();
		a += e - s;
	}
	printf("    spinlock_dequeue: %16" PRIu64 "\n", a / STEPS / (sizeof(spsc_entry) / sizeof(*spsc_entry)));

	a = 0;
	for (i = 0; i < STEPS; i++) {
		ck_shm_fifo_spsc_init(&spsc_fifo, &spsc_stub);

		s = rdtsc();
		for (j = 0; j < ENTRIES; j++)
			ck_shm_fifo_spsc_enqueue(&spsc_fifo, spsc_entry + j, NULL);
		e = rdtsc();

		a += e - s;
	}
	printf("ck_shm_fifo_spsc_enqueue: %16" PRIu64 "\n", a / STEPS / (sizeof(spsc_entry) / sizeof(*spsc_entry)));

	a = 0;
	for (i = 0; i < STEPS; i++) {
		ck_shm_fifo_spsc_init(&spsc_fifo, &spsc_stub);
		for (j = 0; j < ENTRIES; j++)
			ck_shm_fifo_spsc_enqueue(&spsc_fifo, spsc_entry + j, NULL);

		s = rdtsc();
		for (j = 0; j < ENTRIES; j++)
			ck_shm_fifo_spsc_dequeue(&spsc_fifo, &r);
		e = rdtsc();
		a += e - s;
	}
	printf("ck_shm_fifo_spsc_dequeue: %16" PRIu64 "\n", a / STEPS / (sizeof(spsc_entry) / sizeof(*spsc_entry)));
#endif

#ifdef CK_F_SHM_FIFO_MPMC
	a = 0;
	struct entry *temp;
    /*
	for (i = 0; i < STEPS; i++) {
		ck_shm_fifo_mpmc_init(&mpmc_fifo, &mpmc_stub);

		s = rdtsc();
		for (j = 0; j < ENTRIES; j++)
        {
		    temp = mpmc_entry + j;	
            ck_shm_fifo_mpmc_enqueue(&mpmc_fifo, &(temp->fifo_entry));
        }
		e = rdtsc();

		a += e - s;
	}
	printf("ck_shm_fifo_mpmc_enqueue: %16" PRIu64 "\n", a / STEPS / (sizeof(mpmc_entry) / sizeof(*mpmc_entry)));
    */

	a = 0;
	for (i = 0; i < STEPS; i++) {
		ck_shm_fifo_mpmc_init(&mpmc_fifo, &mpmc_stub);
		for (j = 0; j < ENTRIES; j++)
        {
		    temp = mpmc_entry + j;	
            temp->value = j+1;
            ck_shm_fifo_mpmc_enqueue(&mpmc_fifo, &(temp->fifo_entry));
        }

		s = rdtsc();
		for (j = 0; j < ENTRIES; j++)
        {
			ck_shm_fifo_mpmc_dequeue(&mpmc_fifo, &garbage,NULL,NULL);
            fprintf(stdout,"%d\r\n", ((struct entry*)garbage)->value);
        }
		e = rdtsc();
		a += e - s;
	}
	printf("ck_shm_fifo_mpmc_dequeue: %16" PRIu64 "\n", a / STEPS / (sizeof(mpmc_entry) / sizeof(*mpmc_entry)));
#endif

	return 0;
}
