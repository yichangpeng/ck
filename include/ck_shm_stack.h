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

#ifndef CK_SHM_STACK_H
#define CK_SHM_STACK_H

#include <ck_cc.h>
#include <ck_pr.h>
#include <ck_stdbool.h>
#include <ck_stddef.h>
#include <ck_offset_ptr.h>

struct ck_shm_stack_entry;
typedef struct ck_shm_stack_entry ck_shm_stack_entry_t;

ck_offset_ptr(cs_stack_config,
              cs_stack_offset_ptr,
              cs_stack_offset_ptr_clone,
              cs_stack_offset_ptr_get,
              cs_stack_offset_ptr_set,
              cs_stack_offset_ptr_cas,
              ck_shm_stack_entry_t,
              0,47
             )

struct ck_shm_stack_entry {
	cs_stack_offset_ptr next;
};

struct ck_shm_stack {
	cs_stack_offset_ptr head_ptr;
} CK_CC_ALIASED;
typedef struct ck_shm_stack ck_shm_stack_t;

#define CK_SHM_STACK_INITIALIZER { CK_OFFSET_PTR_ENTRY_NULL }

#ifndef CK_F_SHM_STACK_TRYPUSH_UPMC
#define CK_F_SHM_STACK_TRYPUSH_UPMC
/*
 * Stack producer operation for multiple unique producers and multiple consumers.
 * Returns true on success and false on failure.
 */
CK_CC_INLINE static bool
ck_shm_stack_trypush_upmc(struct ck_shm_stack *target, struct ck_shm_stack_entry *entry)
{
    cs_stack_offset_ptr_set(&entry->next,NULL,false,false);
	cs_stack_offset_ptr stack;
    cs_stack_offset_ptr_clone(&target->head_ptr,&stack);

	ck_pr_fence_store();

    cs_stack_offset_ptr_clone(&stack,&entry->next);

	cs_stack_offset_ptr temp;
    cs_stack_offset_ptr_set(&temp,entry,false,false);

	return cs_stack_offset_ptr_cas(&target->head_ptr, &stack, &temp);
}

#endif /* CK_F_SHM_STACK_TRYPUSH_UPMC */

#ifndef CK_F_SHM_STACK_TRYPOP_UPMC
#define CK_F_SHM_STACK_TRYPOP_UPMC
/*
 * Stack production operation for multiple unique producers and multiple consumers.
 * Returns true on success and false on failure. The value pointed to by the second
 * argument is set to a valid ck_shm_stack_entry_t reference if true is returned. If
 * false is returned, then the value pointed to by the second argument is undefined.
 */
CK_CC_INLINE static bool
ck_shm_stack_trypop_upmc(struct ck_shm_stack *target, struct ck_shm_stack_entry **r)
{
	cs_stack_offset_ptr stack;
    cs_stack_offset_ptr_clone(&target->head_ptr,&stack);

    struct ck_shm_stack_entry * next = cs_stack_offset_ptr_get(&stack); 
    if(next == NULL)
        return false;

	ck_pr_fence_store();

	if (cs_stack_offset_ptr_cas(&target->head_ptr, &stack, &next->next) == true) {
        *r = next;
        return true;
    }
    return false;
}
#endif /* CK_F_SHM_STACK_TRYPOP_UPMC */

#ifndef CK_F_SHM_STACK_BATCH_POP_UPMC
#define CK_F_SHM_STACK_BATCH_POP_UPMC
/*
 * Pop all items off the stack.
 */
CK_CC_INLINE static struct ck_shm_stack_entry *
ck_shm_stack_batch_pop_upmc(struct ck_shm_stack *target)
{
	struct ck_shm_stack_entry *entry;

	entry = cs_stack_offset_ptr_get(&target->head_ptr);
    cs_stack_offset_ptr_set(&target->head_ptr,NULL,false,false);
	ck_pr_fence_load();
	return entry;
}
#endif /* CK_F_SHM_STACK_BATCH_POP_UPMC */

#ifndef CK_F_SHM_STACK_PUSH_MPMC
#define CK_F_SHM_STACK_PUSH_MPMC
/*
 * Stack producer operation safe for multiple producers and multiple consumers.
 */
CK_CC_INLINE static void
ck_shm_stack_push_mpmc(struct ck_shm_stack *target, struct ck_shm_stack_entry *entry)
{
    cs_stack_offset_ptr_set(&entry->next,NULL,false,false);
    cs_stack_offset_ptr stack;
    cs_stack_offset_ptr_clone(&target->head_ptr,&stack);

    ck_pr_fence_store();

    cs_stack_offset_ptr_clone(&stack,&entry->next);

    cs_stack_offset_ptr temp;
    cs_stack_offset_ptr_set(&temp,entry,false,false);

    while (cs_stack_offset_ptr_cas(&target->head_ptr, &stack, &temp) == false) 
    {
        cs_stack_offset_ptr_clone(&target->head_ptr,&stack);
        ck_pr_fence_store();
        cs_stack_offset_ptr_clone(&stack,&entry->next);
    }

    return;
}
#endif /* CK_F_SHM_STACK_PUSH_MPMC */

#ifndef CK_F_SHM_STACK_TRYPUSH_MPMC
#define CK_F_SHM_STACK_TRYPUSH_MPMC
/*
 * Stack producer operation safe for multiple producers and multiple consumers.
 */
CK_CC_INLINE static bool
ck_shm_stack_trypush_mpmc(struct ck_shm_stack *target, struct ck_shm_stack_entry *entry)
{

	return ck_shm_stack_trypush_upmc(target, entry);
}
#endif /* CK_F_SHM_STACK_TRYPUSH_MPMC */

#ifdef CK_F_PR_CAS_PTR_2_VALUE
#ifndef CK_F_SHM_STACK_POP_MPMC
#define CK_F_SHM_STACK_POP_MPMC
/*
 * Stack consumer operation safe for multiple producers and multiple consumers.
 */
CK_CC_INLINE static struct ck_shm_stack_entry *
ck_shm_stack_pop_mpmc(struct ck_shm_stack *target)
{
	cs_stack_offset_ptr stack;
    cs_stack_offset_ptr_clone(&target->head_ptr,&stack);

    struct ck_shm_stack_entry * next = cs_stack_offset_ptr_get(&stack); 
    if(next == NULL)
        return NULL;

	ck_pr_fence_store();

	while (cs_stack_offset_ptr_cas(&target->head_ptr, &stack, &next->next) == false) {
        cs_stack_offset_ptr_clone(&target->head_ptr,&stack);
		ck_pr_fence_store();
        next = cs_stack_offset_ptr_get(&stack); 
        if(next == NULL)
            return NULL;
		ck_pr_fence_store();
    }
    return next;
}
#endif /* CK_F_SHM_STACK_POP_MPMC */

#ifndef CK_F_SHM_STACK_TRYPOP_MPMC
#define CK_F_SHM_STACK_TRYPOP_MPMC
CK_CC_INLINE static bool
ck_shm_stack_trypop_mpmc(struct ck_shm_stack *target, struct ck_shm_stack_entry **r)
{
    return ck_shm_stack_trypop_upmc(target,r);
}
#endif /* CK_F_SHM_STACK_TRYPOP_MPMC */
#endif /* CK_F_PR_CAS_PTR_2_VALUE */

#ifndef CK_F_SHM_STACK_BATCH_POP_MPMC
#define CK_F_SHM_STACK_BATCH_POP_MPMC
/*
 * This is equivalent to the UP/MC version as NULL does not need a
 * a generation count.
 */
CK_CC_INLINE static struct ck_shm_stack_entry *
ck_shm_stack_batch_pop_mpmc(struct ck_shm_stack *target)
{
	return ck_shm_stack_batch_pop_upmc(target);
}
#endif /* CK_F_SHM_STACK_BATCH_POP_MPMC */

/*
 * Stack producer operation for single producer and no concurrent consumers.
 */
CK_CC_INLINE static void
ck_shm_stack_push_spnc(struct ck_shm_stack *target, struct ck_shm_stack_entry *entry)
{

    cs_stack_offset_ptr_clone(&target->head_ptr,&entry->next);
    cs_stack_offset_ptr_set(&target->head_ptr,entry,false,false); 
	return;
}

/*
 * Stack consumer operation for no concurrent producers and single consumer.
 */
CK_CC_INLINE static struct ck_shm_stack_entry *
ck_shm_stack_pop_npsc(struct ck_shm_stack *target)
{
	struct ck_shm_stack_entry *n;

    n = cs_stack_offset_ptr_get(&target->head_ptr);
	if (n == NULL)
		return NULL;

    cs_stack_offset_ptr_clone(&n->next,&target->head_ptr);

	return n;
}

/*
 * Pop all items off a stack.
 */
CK_CC_INLINE static struct ck_shm_stack_entry *
ck_shm_stack_batch_pop_npsc(struct ck_shm_stack *target)
{
	struct ck_shm_stack_entry *n;

    n = cs_stack_offset_ptr_get(&target->head_ptr);
    cs_stack_offset_ptr_set(&target->head_ptr,NULL,false,false);

	return n;
}

#ifndef CK_F_SHM_STACK_PUSH_UPMC
#define CK_F_SHM_STACK_PUSH_UPMC
/*
 * Stack producer operation safe for multiple unique producers and multiple consumers.
 */
CK_CC_INLINE static void
ck_shm_stack_push_upmc(struct ck_shm_stack *target, struct ck_shm_stack_entry *entry)
{
    ck_shm_stack_push_mpmc(target,entry);
    return;
}
#endif /* CK_F_SHM_STACK_PUSH_UPMC */

#ifndef CK_F_SHM_STACK_PUSH_MPNC
#define CK_F_SHM_STACK_PUSH_MPNC
/*
 * Stack producer operation safe with no concurrent consumers.
 */
CK_CC_INLINE static void
ck_shm_stack_push_mpnc(struct ck_shm_stack *target, struct ck_shm_stack_entry *entry)
{
    ck_shm_stack_push_mpmc(target,entry);
	return;
}
#endif /* CK_F_SHM_STACK_PUSH_MPNC */

/*
 * Stack initialization function. Guarantees initialization across processors.
 */
CK_CC_INLINE static void
ck_shm_stack_init(struct ck_shm_stack *stack)
{
    cs_stack_offset_ptr_set(&stack->head_ptr,NULL,false,false);
	return;
}

#ifndef CK_F_SHM_STACK_POP_UPMC
#define CK_F_SHM_STACK_POP_UPMC
/*
 * Stack consumer operation safe for multiple unique producers and multiple consumers.
 */
CK_CC_INLINE static struct ck_shm_stack_entry *
ck_shm_stack_pop_upmc(struct ck_shm_stack *target)
{
    return ck_shm_stack_pop_mpmc(target);
}
#endif

/* Defines a container_of functions for */
#define CK_SHM_STACK_CONTAINER(T, M, N) CK_CC_CONTAINER(ck_shm_stack_entry_t, T, M, N)

#define CK_SHM_STACK_ISEMPTY(m) ( is_offset_ptr_null(cs_stack_config,&((m)->head_ptr)) )
#define CK_SHM_STACK_FIRST(s)   (cs_stack_offset_ptr_get(&((s)->head_ptr)))
#define CK_SHM_STACK_NEXT(m)    (cs_stack_offset_ptr_get(&((m)->next)))
#define CK_SHM_STACK_FOREACH(stack, entry)				\
	for ((entry) = CK_SHM_STACK_FIRST(stack);			\
	     (entry) != NULL;					\
	     (entry) = CK_SHM_STACK_NEXT(entry))
#define CK_SHM_STACK_FOREACH_SAFE(stack, entry, T)			\
	for ((entry) = CK_SHM_STACK_FIRST(stack);			\
	     (entry) != NULL && ((T) = cs_stack_offset_ptr_get(&(entry)->next), 1);	\
	     (entry) = (T))

#endif /* CK_SHM_STACK_H */
