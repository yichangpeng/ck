/*
 * Copyright 2009 Samy Al Bahra.
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

#include <assert.h>
#include <ck_cc.h>
#include <ck_pr.h>
#include <ck_shm_fifo.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#include "../../common.h"

#ifndef ITEMS
#define ITEMS (5765760)
//#define ITEMS (1)
#endif

#define TVTOD(tv) ((tv).tv_sec+((tv).tv_usec / (double)1000000))
#define CK_SHM_FIFO_CONTAINER(T, M, N) CK_CC_CONTAINER(ck_shm_fifo_mpmc_entry_t, T, M, N)

static unsigned int push_threads_count = 0;
static unsigned int push_count = 0;
static unsigned int pop_count = 0;
struct entry {
	int value;
	ck_shm_fifo_mpmc_entry_t next;
} CK_CC_CACHELINE;

static ck_shm_fifo_mpmc_t fifo CK_CC_CACHELINE;
CK_SHM_FIFO_CONTAINER(struct entry, next, getvalue)

static struct affinity affinerator;
static unsigned long long nthr;
static volatile unsigned int barrier = 0;
static unsigned int critical;

static void *
fifo_push_thread(void* unused CK_CC_UNUSED)
{
	struct entry *entry = NULL;
	unsigned long long i, n = ITEMS;

	if (aff_iterate(&affinerator)) {
		perror("ERROR: failed to affine thread");
		exit(EXIT_FAILURE);
	}

	while (barrier == 0);

	for (i = 0; i < n; i++) {
        entry = malloc(sizeof(struct entry));
        entry->value = i;
#if defined(MPMC)
        ck_shm_fifo_mpmc_enqueue(&fifo, &entry->next);
#elif defined(TRYMPMC)
		while (ck_shm_fifo_mpmc_tryenqueue(&fifo, &entry->next) == false)
			ck_pr_stall();
#else
#     error Undefined operation.
#endif
        ck_pr_inc_uint(&push_count);
        //fprintf(stdout,"push:%lu,vaule:%d,count:%u\r\n",syscall(SYS_gettid),entry->value,ck_pr_load_uint(&push_count));
	}

    ck_pr_dec_uint(&push_threads_count);
	return (NULL);
}

static void *
fifo_pop_thread(void* unused CK_CC_UNUSED)
{
#if (defined(MPMC) && defined(CK_F_SHM_FIFO_MPMC)) || (defined(TRYMPMC))
	ck_shm_fifo_mpmc_entry_t *ref = NULL;
#endif

	struct entry *entry = NULL;
	unsigned int seed;
	int j;

	if (aff_iterate(&affinerator)) {
		perror("ERROR: failed to affine thread");
		exit(EXIT_FAILURE);
	}

	while (barrier == 0);

	while(true) {
		if (critical) {
			j = common_rand_r(&seed) % critical;
			while (j--)
				__asm__ __volatile__("" ::: "memory");
		}

#ifdef CK_F_SHM_FIFO_MPMC
#if defined(MPMC)
		ck_shm_fifo_mpmc_dequeue(&fifo, &ref, NULL, NULL);
        if(ref == NULL)
        {
            if(ck_pr_load_uint(&push_threads_count) == 0)
                break;
            else{
                usleep(500);
                continue;
            }
        }
		entry = getvalue(ref);
#elif defined(TRYMPMC)
		if (ck_shm_fifo_mpmc_trydequeue(&fifo, &ref, NULL, NULL) == false)
        {
            if(ck_pr_load_uint(&push_threads_count) == 0)
                break;
            else{
                usleep(500);
                continue;
            }
            ck_pr_stall();
        }
	
		entry = getvalue(ref);
#endif
#else
#		error Undefined operation.
#endif
        ck_pr_inc_uint(&pop_count);
        //if(ref && entry)
            //fprintf(stdout,"pop:%lu,vaule:%d,count:%u\r\n",syscall(SYS_gettid),entry->value,ck_pr_load_uint(&pop_count));

        ref = NULL;
        //can not free,pop may return earlier than push,if free the entry,the push thread can not access the entry
        //free(entry);
        entry = NULL;
	}

	return (NULL);
}
static void
fifo_assert(void)
{
	assert(CK_SHM_FIFO_MPMC_ISEMPTY(&fifo));
	return;
}

int
main(int argc, char *argv[])
{
	unsigned long long i, d;
	pthread_t *thread;
	struct timeval stv, etv;

#if (defined(TRYMPMC) || defined(MPMC)) && (!defined(CK_F_SHM_FIFO_MPMC))
        fprintf(stderr, "Unsupported.\n");
        return 0;
#endif

	if (argc != 4) {
		ck_error("Usage: fifo <threads> <delta> <critical>\n");
	}

	{
		char *e;

		nthr = strtol(argv[1], &e, 10);
		if (errno == ERANGE) {
			perror("ERROR: too many threads");
			exit(EXIT_FAILURE);
		} else if (*e != '\0') {
			ck_error("ERROR: input format is incorrect\n");
		}

		d = strtol(argv[2], &e, 10);
		if (errno == ERANGE) {
			perror("ERROR: delta is too large");
			exit(EXIT_FAILURE);
		} else if (*e != '\0') {
			ck_error("ERROR: input format is incorrect\n");
		}

		critical = strtoul(argv[3], &e, 10);
		if (errno == ERANGE) {
			perror("ERROR: critical section is too large");
			exit(EXIT_FAILURE);
		} else if (*e != '\0') {
			ck_error("ERROR: input format is incorrect\n");
		}
	}

	srand(getpid());

	affinerator.request = 0;
	affinerator.delta = d;

	thread = malloc(sizeof(pthread_t) * nthr);
	assert(thread != NULL);

#if (defined(MPMC) && defined(CK_F_SHM_FIFO_MPMC)) || (defined(TRYMPMC) && defined(CK_F_SHM_FIFO_MPMC))
    ck_shm_fifo_mpmc_entry_t * dummy = malloc(sizeof(ck_shm_fifo_mpmc_entry_t));
    ck_shm_fifo_mpmc_init(&fifo,dummy);
#endif

    push_threads_count = (nthr+1)/2;
   	barrier = 0;

    for (i = 0; i < nthr; i++){
        if(i%2 == 0)
		    pthread_create(&thread[i], NULL, fifo_push_thread, NULL);
        else
		    pthread_create(&thread[i], NULL, fifo_pop_thread, NULL);
    }

	common_gettimeofday(&stv, NULL);
	barrier = 1;
	for (i = 0; i < nthr; i++)
		pthread_join(thread[i], NULL);
	common_gettimeofday(&etv, NULL);

	fifo_assert();
#ifdef _WIN32
	printf("%3llu %.6f\n", nthr, TVTOD(etv) - TVTOD(stv));
#else
    printf("push:%10u pop:%10u -> %3llu %.6lf\n",push_count,pop_count,nthr, TVTOD(etv) - TVTOD(stv));
#endif
	return 0;
}
