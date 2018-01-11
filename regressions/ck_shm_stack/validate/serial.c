/*
 * Copyright 2009-2015 Samy Al Bahra.
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
#include <stdlib.h>
#include <stdio.h>
#include <ck_shm_stack.h>
#include "../../common.h"

#ifndef SIZE
#define SIZE 1024000
#endif

struct entry {
	int value;
	ck_shm_stack_entry_t next;
};

CK_SHM_STACK_CONTAINER(struct entry, next, get_entry)

#define LOOP(PUSH, POP)								\
    common_gettimeofday(&start,NULL);              \
	for (i = 0; i < SIZE; i++) {						\
		entries[i].value = i;						\
		PUSH(stack, &entries[i].next);					\
	}									\
    common_gettimeofday(&end,NULL);              \
    push_eplased = (end.tv_sec-start.tv_sec)*1000+(end.tv_usec-start.tv_usec)/1000; \
	for (i = SIZE - 1; i >= 0; i--) {					\
		entry = POP(stack);						\
		assert(entry);							\
		assert(get_entry(entry)->value == i);				\
	}                                           \
    common_gettimeofday(&start,NULL);              \
    pop_eplased = (start.tv_sec-end.tv_sec)*1000+(start.tv_usec-end.tv_usec)/1000; \
         fprintf(stderr,"%s_eplased=%lu,%s_eplased=%lu\r\n",#PUSH,push_eplased,#POP,pop_eplased);

static void
serial(ck_shm_stack_t *stack)
{
	struct entry *entries;
	ck_shm_stack_entry_t *entry;
	int i;

	ck_shm_stack_init(stack);

	entries = malloc(sizeof(struct entry) * SIZE);
	assert(entries != NULL);
    
    struct timeval start,end;
    size_t push_eplased, pop_eplased;

	LOOP(ck_shm_stack_push_upmc, ck_shm_stack_pop_upmc);
#ifdef CK_F_SHM_STACK_POP_MPMC
	LOOP(ck_shm_stack_push_mpmc, ck_shm_stack_pop_mpmc);
#endif
    LOOP(ck_shm_stack_push_mpnc, ck_shm_stack_pop_upmc);
    LOOP(ck_shm_stack_push_spnc, ck_shm_stack_pop_npsc);

	return;
}

int
main(void)
{
	ck_shm_stack_t stack CK_CC_CACHELINE;

	serial(&stack);
	return (0);
}
