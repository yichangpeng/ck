/*
 * Copyright 2011 David Joseph.
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
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

struct example {
    ck_shm_fifo_spsc_entry_t entry;
	int x;
};

static ck_shm_fifo_spsc_t spsc_fifo;

int
main(void)
{
	int i, length = 3;
	struct example *examples;
	ck_shm_fifo_spsc_entry_t *stub, *entry, *next;

	stub = malloc(sizeof(ck_shm_fifo_spsc_entry_t));
	if (stub == NULL)
		exit(EXIT_FAILURE);

	ck_shm_fifo_spsc_init(&spsc_fifo, stub);

	examples = malloc(sizeof(struct example) * length);
	/* Need these for this unit test. */
	if (examples == NULL)
		exit(EXIT_FAILURE);

	for (i = 0; i < length; ++i) {
		examples[i].x = i;
		ck_shm_fifo_spsc_enqueue(&spsc_fifo, (ck_shm_fifo_spsc_entry_t*)(examples + i));
	}

	puts("TESTING CK_SHM_FIFO_SPSC_FOREACH.");
	CK_SHM_FIFO_SPSC_FOREACH(&spsc_fifo, entry) {
		printf("Next value in fifo: %d\n", (((struct example *)(entry))->x));
	}

	puts("Testing CK_SHM_FIFO_SPSC_FOREACH_SAFE.");
	CK_SHM_FIFO_SPSC_FOREACH_SAFE(&spsc_fifo, entry, next) {
		if (cs_fifo_spsc_offset_ptr_get(&entry->next) != next)
			exit(EXIT_FAILURE);
		printf("Next value in fifo: %d\n", (((struct example *)(entry))->x));
	}

	free(examples);
	free(stub);

	return (0);
}

