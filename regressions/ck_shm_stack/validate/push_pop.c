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
#ifdef SPINLOCK
#include <ck_spinlock.h>
#endif
#include <ck_shm_stack.h>
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
//#define ITEMS (10000)
#endif

#define TVTOD(tv) ((tv).tv_sec+((tv).tv_usec / (double)1000000))

static unsigned int push_threads_count = 0;
static unsigned int push_count = 0;
static unsigned int pop_count = 0;
struct entry {
	int value;
#if defined(SPINLOCK) || defined(PTHREADS)
	struct entry *next;
#else
	ck_shm_stack_entry_t next;
#endif
} CK_CC_CACHELINE;

#ifdef SPINLOCK
static struct entry *stack CK_CC_CACHELINE;
ck_spinlock_fas_t stack_spinlock = CK_SPINLOCK_FAS_INITIALIZER;
#define UNLOCK ck_spinlock_fas_unlock
#if defined(EB)
#define LOCK ck_spinlock_fas_lock_eb
#else
#define LOCK ck_spinlock_fas_lock
#endif
#elif defined(PTHREADS)
static struct entry *stack CK_CC_CACHELINE;
pthread_mutex_t stack_spinlock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK pthread_mutex_lock
#define UNLOCK pthread_mutex_unlock
#else
static ck_shm_stack_t stack CK_CC_CACHELINE;
CK_SHM_STACK_CONTAINER(struct entry, next, getvalue)
#endif

static struct affinity affinerator;
static unsigned long long nthr;
static volatile unsigned int barrier = 0;
static unsigned int critical;

static void *
stack_push_thread(void* unused CK_CC_UNUSED)
{
	struct entry *entry;
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
                ck_shm_stack_push_mpmc(&stack, &entry->next);
#elif defined(TRYMPMC)
		while (ck_shm_stack_trypush_mpmc(&stack, &entry->next) == false)
			ck_pr_stall();
#elif defined(UPMC)
                ck_shm_stack_push_upmc(&stack, &entry->next);
#elif defined(TRYUPMC)
		while (ck_shm_stack_trypush_upmc(&stack, &entry->next) == false)
			ck_pr_stall();
#elif defined(SPINLOCK) || defined(PTHREADS)
		LOCK(&stack_spinlock);
		ck_pr_store_ptr(&entry->next, stack);
		ck_pr_store_ptr(&stack, entry);
		UNLOCK(&stack_spinlock);
#else
#               error Undefined operation.
#endif
        ck_pr_inc_uint(&push_count);
        //fprintf(stdout,"push:%lu,vaule:%d,count:%u\r\n",syscall(SYS_gettid),entry->value,ck_pr_load_uint(&push_count));
	}

    ck_pr_dec_uint(&push_threads_count);
	return (NULL);
}

static void *
stack_pop_thread(void* unused CK_CC_UNUSED)
{
#if (defined(MPMC) && defined(CK_F_SHM_STACK_POP_MPMC)) || (defined(UPMC) && defined(CK_F_SHM_STACK_POP_UPMC)) || (defined(TRYUPMC) && defined(CK_F_SHM_STACK_TRYPOP_UPMC)) || (defined(TRYMPMC) && defined(CK_F_SHM_STACK_TRYPOP_MPMC))
	ck_shm_stack_entry_t *ref;
#endif

	struct entry *entry;
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

#if defined(MPMC)
#ifdef CK_F_SHM_STACK_POP_MPMC
		ref = ck_shm_stack_pop_mpmc(&stack);
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
#endif
#elif defined(TRYMPMC)
#ifdef CK_F_SHM_STACK_TRYPOP_MPMC
		if (ck_shm_stack_trypop_mpmc(&stack, &ref) == false)
        {
            if(ref == NULL)
            {
                if(ck_pr_load_uint(&push_threads_count) == 0)
                    break;
                else{
                    usleep(500);
                    continue;
                }
            }
            ck_pr_stall();
        }
	
		entry = getvalue(ref);
#endif /* CK_F_SHM_STACK_TRYPOP_MPMC */
#elif defined(UPMC)
		ref = ck_shm_stack_pop_upmc(&stack);
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
#elif defined(SPINLOCK) || defined(PTHREADS)
		LOCK(&stack_spinlock);
		entry = stack;
        if(entry == NULL)
        {
            if(ck_pr_load_uint(&push_threads_count) == 0)
                break;
            else{
                usleep(500);
                continue;
            }
        }

		stack = stack->next;
		UNLOCK(&stack_spinlock);
#else
#		error Undefined operation.
#endif
        ck_pr_inc_uint(&pop_count);
        //fprintf(stdout,"pop:%lu,vaule:%d,count:%u\r\n",syscall(SYS_gettid),entry->value,ck_pr_load_uint(&pop_count));
        free(entry);
	}

	return (NULL);
}
static void
stack_assert(void)
{

#if defined(SPINLOCK) || defined(PTHREADS)
	assert(stack == NULL);
#else
	assert(CK_SHM_STACK_ISEMPTY(&stack));
#endif
	return;
}

int
main(int argc, char *argv[])
{
	unsigned long long i, d;
	pthread_t *thread;
	struct timeval stv, etv;

#if (defined(TRYMPMC) || defined(MPMC)) && (!defined(CK_F_SHM_STACK_PUSH_MPMC) || !defined(CK_F_SHM_STACK_POP_MPMC))
        fprintf(stderr, "Unsupported.\n");
        return 0;
#endif

	if (argc != 4) {
		ck_error("Usage: stack <threads> <delta> <critical>\n");
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

#if (defined(MPMC) && defined(CK_F_SHM_STACK_POP_MPMC)) || (defined(UPMC) && defined(CK_F_SHM_STACK_POP_UPMC)) || (defined(TRYUPMC) && defined(CK_F_SHM_STACK_TRYPOP_UPMC)) || (defined(TRYMPMC) && defined(CK_F_SHM_STACK_TRYPOP_MPMC))
    ck_shm_stack_init(&stack);
#endif

    push_threads_count = (nthr+1)/2;
   	barrier = 0;

    for (i = 0; i < nthr; i++){
        if(i%2 == 0)
		    pthread_create(&thread[i], NULL, stack_push_thread, NULL);
        else
		    pthread_create(&thread[i], NULL, stack_pop_thread, NULL);
    }

	common_gettimeofday(&stv, NULL);
	barrier = 1;
	for (i = 0; i < nthr; i++)
		pthread_join(thread[i], NULL);
	common_gettimeofday(&etv, NULL);

	stack_assert();
#ifdef _WIN32
	printf("%3llu %.6f\n", nthr, TVTOD(etv) - TVTOD(stv));
#else
    printf("push:%10u pop:%10u -> %3llu %.6lf\n",push_count,pop_count,nthr, TVTOD(etv) - TVTOD(stv));
#endif
	return 0;
}
