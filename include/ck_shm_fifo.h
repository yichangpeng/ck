/*
 * Copyright 2010-2015 Samy Al Bahra.
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

#ifndef CK_SHM_FIFO_H
#define CK_SHM_FIFO_H

#include <ck_cc.h>
#include <ck_md.h>
#include <ck_pr.h>
#include <ck_spinlock.h>
#include <ck_stddef.h>
#include <ck_offset_ptr.h>

#ifndef CK_F_SHM_FIFO_SPSC
#define CK_F_SHM_FIFO_SPSC

struct ck_shm_fifo_spsc_entry;
typedef struct ck_shm_fifo_spsc_entry ck_shm_fifo_spsc_entry_t;

ck_offset_ptr(cs_fifo_spsc_config,
              cs_fifo_spsc_offset_ptr,
              cs_fifo_spsc_offset_ptr_clone,
              cs_fifo_spsc_offset_ptr_get,
              cs_fifo_spsc_offset_ptr_set,
              cs_fifo_spsc_offset_ptr_cas,
              ck_shm_fifo_spsc_entry_t,
              0,47
             )

struct ck_shm_fifo_spsc_entry {
    cs_fifo_spsc_offset_ptr next;
};


struct ck_shm_fifo_spsc {
	ck_spinlock_t m_head;
    cs_fifo_spsc_offset_ptr head;
	char pad[CK_MD_CACHELINE - sizeof(cs_fifo_spsc_offset_ptr) - sizeof(ck_spinlock_t)];
	ck_spinlock_t m_tail;
    cs_fifo_spsc_offset_ptr tail;
    cs_fifo_spsc_offset_ptr head_snapshot;
    cs_fifo_spsc_offset_ptr garbage;
};
typedef struct ck_shm_fifo_spsc ck_shm_fifo_spsc_t;

CK_CC_INLINE static bool
ck_shm_fifo_spsc_enqueue_trylock(struct ck_shm_fifo_spsc *fifo)
{

	return ck_spinlock_trylock(&fifo->m_tail);
}

CK_CC_INLINE static void
ck_shm_fifo_spsc_enqueue_lock(struct ck_shm_fifo_spsc *fifo)
{

	ck_spinlock_lock(&fifo->m_tail);
	return;
}

CK_CC_INLINE static void
ck_shm_fifo_spsc_enqueue_unlock(struct ck_shm_fifo_spsc *fifo)
{

	ck_spinlock_unlock(&fifo->m_tail);
	return;
}

CK_CC_INLINE static bool
ck_shm_fifo_spsc_dequeue_trylock(struct ck_shm_fifo_spsc *fifo)
{

	return ck_spinlock_trylock(&fifo->m_head);
}

CK_CC_INLINE static void
ck_shm_fifo_spsc_dequeue_lock(struct ck_shm_fifo_spsc *fifo)
{

	ck_spinlock_lock(&fifo->m_head);
	return;
}

CK_CC_INLINE static void
ck_shm_fifo_spsc_dequeue_unlock(struct ck_shm_fifo_spsc *fifo)
{

	ck_spinlock_unlock(&fifo->m_head);
	return;
}

CK_CC_INLINE static void
ck_shm_fifo_spsc_init(struct ck_shm_fifo_spsc *fifo, struct ck_shm_fifo_spsc_entry *stub)
{

	ck_spinlock_init(&fifo->m_head);
	ck_spinlock_init(&fifo->m_tail);

	cs_fifo_spsc_offset_ptr_set(&stub->next,NULL,false,false);
	cs_fifo_spsc_offset_ptr_set(&fifo->head,stub,false,false);
	cs_fifo_spsc_offset_ptr_set(&fifo->tail,stub,false,false);
	cs_fifo_spsc_offset_ptr_set(&fifo->head_snapshot,stub,false,false);
	cs_fifo_spsc_offset_ptr_set(&fifo->garbage,stub,false,false);
	return;
}

CK_CC_INLINE static void
ck_shm_fifo_spsc_deinit(struct ck_shm_fifo_spsc *fifo, struct ck_shm_fifo_spsc_entry **garbage)
{

	*garbage = cs_fifo_spsc_offset_ptr_get(&fifo->head);
	cs_fifo_spsc_offset_ptr_set(&fifo->head,NULL,false,false);
	cs_fifo_spsc_offset_ptr_set(&fifo->tail,NULL,false,false);
	return;
}

CK_CC_INLINE static void
ck_shm_fifo_spsc_enqueue(struct ck_shm_fifo_spsc *fifo,
		     struct ck_shm_fifo_spsc_entry *entry)
{
	cs_fifo_spsc_offset_ptr_set(&entry->next,NULL,false,false);

	/* If stub->next is visible, guarantee that entry is consistent. */
	ck_pr_fence_store();
    struct ck_shm_fifo_spsc_entry * tail_snapshot = cs_fifo_spsc_offset_ptr_get(&fifo->tail);
    cs_fifo_spsc_offset_ptr_set(&tail_snapshot->next,entry,false,false);
    cs_fifo_spsc_offset_ptr_set(&fifo->tail,entry,false,false);
	return;
}

CK_CC_INLINE static bool
ck_shm_fifo_spsc_dequeue(struct ck_shm_fifo_spsc *fifo, struct ck_shm_fifo_spsc_entry **value)
{
	struct ck_shm_fifo_spsc_entry *entry;

	/*
	 * The head pointer is guaranteed to always point to a stub entry.
	 * If the stub entry does not point to an entry, then the queue is
	 * empty.
	 */
    entry = cs_fifo_spsc_offset_ptr_get(&fifo->head);
	entry = cs_fifo_spsc_offset_ptr_get(&entry->next);
	if (entry == NULL)
		return false;

    *value = entry; 

	ck_pr_fence_store();
    cs_fifo_spsc_offset_ptr_set(&fifo->head,entry,false,false);
	return true;
}

/*
 * Recycle a node. This technique for recycling nodes is based on
 * Dmitriy Vyukov's work.
 */
CK_CC_INLINE static struct ck_shm_fifo_spsc_entry * 
ck_shm_fifo_spsc_recycle(struct ck_shm_fifo_spsc *fifo)
{
	struct ck_shm_fifo_spsc_entry *garbage;

	if (cs_fifo_spsc_offset_ptr_get(&fifo->head_snapshot) == cs_fifo_spsc_offset_ptr_get(&fifo->garbage)) {
        cs_fifo_spsc_offset_ptr_clone(&fifo->head,&fifo->head_snapshot);
	    if (cs_fifo_spsc_offset_ptr_get(&fifo->head_snapshot) == cs_fifo_spsc_offset_ptr_get(&fifo->garbage)) {
			return NULL;
	    }
    }

	garbage = cs_fifo_spsc_offset_ptr_get(&fifo->garbage);
    cs_fifo_spsc_offset_ptr_clone(&garbage->next,&fifo->garbage);
	return garbage;
}

CK_CC_INLINE static bool 
ck_shm_fifo_spsc_isempty(struct ck_shm_fifo_spsc *fifo)
{
	struct ck_shm_fifo_spsc_entry *head = cs_fifo_spsc_offset_ptr_get(&fifo->head);
	return cs_fifo_spsc_offset_ptr_get(&head->next) == NULL;
}

#define CK_SHM_FIFO_SPSC_ISEMPTY(f) ( is_offset_ptr_null(cs_fifo_spsc_config,&((f)->head)) )
#define CK_SHM_FIFO_SPSC_FIRST(f)   (cs_fifo_spsc_offset_ptr_get(&cs_fifo_spsc_offset_ptr_get(&((f)->head))->next))
#define CK_SHM_FIFO_SPSC_NEXT(m)	(cs_fifo_spsc_offset_ptr_get(&(m)->next))
#define CK_SHM_FIFO_SPSC_FOREACH(fifo, entry)				\
	for ((entry) = CK_SHM_FIFO_SPSC_FIRST(fifo);			\
	     (entry) != NULL;						\
	     (entry) = CK_SHM_FIFO_SPSC_NEXT(entry))
#define CK_SHM_FIFO_SPSC_FOREACH_SAFE(fifo, entry, T)			\
	for ((entry) = CK_SHM_FIFO_SPSC_FIRST(fifo);			\
	     (entry) != NULL && ((T) = cs_fifo_spsc_offset_ptr_get(&(entry)->next), 1);	\
	     (entry) = (T))


#endif /* CK_F_SHM_FIFO_SPSC */


#ifdef CK_F_PR_CAS_PTR_2
#ifndef CK_F_SHM_FIFO_MPMC
#define CK_F_SHM_FIFO_MPMC
struct ck_shm_fifo_mpmc_entry;
typedef struct ck_shm_fifo_mpmc_entry ck_shm_fifo_mpmc_entry_t;

ck_offset_ptr(cs_fifo_mpmc_config,
              cs_fifo_mpmc_offset_ptr,
              cs_fifo_mpmc_offset_ptr_clone,
              cs_fifo_mpmc_offset_ptr_get,
              cs_fifo_mpmc_offset_ptr_set,
              cs_fifo_mpmc_offset_ptr_cas,
              ck_shm_fifo_mpmc_entry_t,
              0,47
             )

struct ck_shm_fifo_mpmc_entry {
    cs_fifo_mpmc_offset_ptr next;
};

struct ck_shm_fifo_mpmc {
	cs_fifo_mpmc_offset_ptr head;
	cs_fifo_mpmc_offset_ptr tail;
};
typedef struct ck_shm_fifo_mpmc ck_shm_fifo_mpmc_t;

// The Baskets Queue.pdf
CK_CC_INLINE static void
ck_shm_fifo_mpmc_init(struct ck_shm_fifo_mpmc *fifo, struct ck_shm_fifo_mpmc_entry *stub)
{

    cs_fifo_mpmc_offset_ptr_set(&stub->next,NULL,false,false);
    cs_fifo_mpmc_offset_ptr_set(&fifo->head,stub,false,false);
    cs_fifo_mpmc_offset_ptr_set(&fifo->tail,stub,false,false);
	return;
}

CK_CC_INLINE static void
ck_shm_fifo_mpmc_deinit(struct ck_shm_fifo_mpmc *fifo, struct ck_shm_fifo_mpmc_entry **garbage)
{

	*garbage = cs_fifo_mpmc_offset_ptr_get(&fifo->head);
    cs_fifo_mpmc_offset_ptr_set(&fifo->head,NULL,false,false);
    cs_fifo_mpmc_offset_ptr_set(&fifo->tail,NULL,false,false);
	return;
}

CK_CC_INLINE static void
ck_shm_fifo_mpmc_try_fix_fail(struct ck_shm_fifo_mpmc *fifo,
            struct ck_shm_fifo_mpmc_entry * next_snapshot, struct ck_shm_fifo_mpmc_entry *tail_snapshot){

    while(cs_fifo_mpmc_offset_ptr_get(&next_snapshot->next) != NULL 
          && cs_fifo_mpmc_offset_ptr_get(&fifo->tail) == tail_snapshot)
    {
        next_snapshot = cs_fifo_mpmc_offset_ptr_get(&next_snapshot->next);
    }

    cs_fifo_mpmc_offset_ptr_cas_ptr(&fifo->tail, tail_snapshot, next_snapshot);
    return;
}

CK_CC_INLINE static void
ck_shm_fifo_mpmc_enqueue(struct ck_shm_fifo_mpmc *fifo,
		     struct ck_shm_fifo_mpmc_entry *entry)
{
	cs_fifo_mpmc_offset_ptr tail = CK_OFFSET_PTR_ENTRY_NULL, next = CK_OFFSET_PTR_ENTRY_NULL, update = CK_OFFSET_PTR_ENTRY_NULL;
    struct ck_shm_fifo_mpmc_entry * next_snapshot, * tail_snapshot;

	cs_fifo_mpmc_offset_ptr_set(&entry->next,NULL,false,false);
    cs_fifo_mpmc_offset_ptr_set(&update,entry,false,false);
	ck_pr_fence_store_atomic();

	for (;;) {
        cs_fifo_mpmc_offset_ptr_clone(&fifo->tail,&tail);
		ck_pr_fence_load();
        tail_snapshot = cs_fifo_mpmc_offset_ptr_get(&tail);

        if(cs_fifo_mpmc_offset_ptr_get(&fifo->tail) == tail_snapshot){
            cs_fifo_mpmc_offset_ptr_clone(&tail_snapshot->next,&next);
            ck_pr_fence_load();

            next_snapshot = cs_fifo_mpmc_offset_ptr_get(&next);
            if (next_snapshot == NULL){
                if(cs_fifo_mpmc_offset_ptr_cas(&tail_snapshot->next,&next,&update) == true){
                    cs_fifo_mpmc_offset_ptr_cas(&fifo->tail,&tail,&update);
                    return;
                }
                cs_fifo_mpmc_offset_ptr_clone(&tail_snapshot->next,&next); 
                while(false == is_offset_ptr_marked(cs_fifo_mpmc_config,&next)){
                    cs_fifo_mpmc_offset_ptr_clone(&next,&entry->next);
                    if(cs_fifo_mpmc_offset_ptr_cas(&tail_snapshot->next,&next,&update) == true){
                        return;
                    }
                    cs_fifo_mpmc_offset_ptr_clone(&tail_snapshot->next,&next); 
                }
            }
            else{
                ck_shm_fifo_mpmc_try_fix_fail(fifo, next_snapshot, tail_snapshot);
            }
        }
	}

	ck_pr_fence_atomic();

	return;
}

CK_CC_INLINE static bool
ck_shm_fifo_mpmc_tryenqueue(struct ck_shm_fifo_mpmc *fifo,
		        struct ck_shm_fifo_mpmc_entry *entry)
{
    cs_fifo_mpmc_offset_ptr tail = CK_OFFSET_PTR_ENTRY_NULL, next = CK_OFFSET_PTR_ENTRY_NULL, update = CK_OFFSET_PTR_ENTRY_NULL;
    struct ck_shm_fifo_mpmc_entry * next_snapshot, * tail_snapshot;

    cs_fifo_mpmc_offset_ptr_set(&entry->next,NULL,false,false);
    cs_fifo_mpmc_offset_ptr_set(&update,entry,false,false);
    ck_pr_fence_store_atomic();

    cs_fifo_mpmc_offset_ptr_clone(&fifo->tail,&tail);
    ck_pr_fence_load();
    tail_snapshot = cs_fifo_mpmc_offset_ptr_get(&tail);

    if(cs_fifo_mpmc_offset_ptr_get(&fifo->tail) == tail_snapshot){
        cs_fifo_mpmc_offset_ptr_clone(&tail_snapshot->next,&next);
        ck_pr_fence_load();

        next_snapshot = cs_fifo_mpmc_offset_ptr_get(&next);
        if (next_snapshot == NULL){
            if(cs_fifo_mpmc_offset_ptr_cas(&tail_snapshot->next,&next,&update) == true){
                cs_fifo_mpmc_offset_ptr_cas(&fifo->tail,&tail,&update);
                return true;
            }
            cs_fifo_mpmc_offset_ptr_clone(&tail_snapshot->next,&next); 
            while(false == is_offset_ptr_marked(cs_fifo_mpmc_config,&next)){
                cs_fifo_mpmc_offset_ptr_clone(&next,&entry->next);
                if(cs_fifo_mpmc_offset_ptr_cas(&tail_snapshot->next,&next,&update) == true){
                    return true;
                }
                cs_fifo_mpmc_offset_ptr_clone(&tail_snapshot->next,&next); 
            }
        }
        else{
            ck_shm_fifo_mpmc_try_fix_fail(fifo, next_snapshot, tail_snapshot);
        }
    }

    ck_pr_fence_atomic();

    return false;
}

typedef void (*free_node_fun)(struct ck_shm_fifo_mpmc_entry* n, const void * data);

CK_CC_INLINE static void
ck_shm_fifo_mpmc_free_chain(struct ck_shm_fifo_mpmc *fifo, struct ck_shm_fifo_mpmc_entry *head_snapshot, 
            struct ck_shm_fifo_mpmc_entry *newhead_snapshot, free_node_fun free_fun, const void* free_fun_data)
{
    if(cs_fifo_mpmc_offset_ptr_cas_ptr(&fifo->head, head_snapshot, newhead_snapshot)){
        while(head_snapshot != newhead_snapshot){
            head_snapshot = cs_fifo_mpmc_offset_ptr_get(&head_snapshot->next);
            if(free_fun)
                free_fun(head_snapshot, free_fun_data);
        }
    }
}

CK_CC_INLINE static bool
ck_shm_fifo_mpmc_dequeue(struct ck_shm_fifo_mpmc *fifo,
		     struct ck_shm_fifo_mpmc_entry **value, free_node_fun free_fun, const void * free_fun_data)
{
    const int MAX_HOPS = 3;
    cs_fifo_mpmc_offset_ptr head = CK_OFFSET_PTR_ENTRY_NULL, tail = CK_OFFSET_PTR_ENTRY_NULL, next = CK_OFFSET_PTR_ENTRY_NULL, 
    iterator = CK_OFFSET_PTR_ENTRY_NULL, new_next = CK_OFFSET_PTR_ENTRY_NULL;
    struct ck_shm_fifo_mpmc_entry * next_snapshot, * head_snapshot, * tail_snapshot, *iterator_snapshot, * nn, * in;

    while(true){
        cs_fifo_mpmc_offset_ptr_clone(&fifo->head,&head);
        ck_pr_fence_load();
        cs_fifo_mpmc_offset_ptr_clone(&fifo->tail,&tail);
        ck_pr_fence_load();
        head_snapshot = cs_fifo_mpmc_offset_ptr_get(&head);
        cs_fifo_mpmc_offset_ptr_clone(&head_snapshot->next,&next);

        tail_snapshot = cs_fifo_mpmc_offset_ptr_get(&tail);

        if(cs_fifo_mpmc_offset_ptr_get(&fifo->head) == head_snapshot){

            cs_fifo_mpmc_offset_ptr_clone(&head_snapshot->next,&next);
            ck_pr_fence_load();
            next_snapshot = cs_fifo_mpmc_offset_ptr_get(&next);
            if (head_snapshot == tail_snapshot){
                if(next_snapshot == NULL)
                    return false;
                ck_shm_fifo_mpmc_try_fix_fail(fifo, next_snapshot, tail_snapshot);
            }
            else{
                cs_fifo_mpmc_offset_ptr_clone(&head,&iterator);
                iterator_snapshot = head_snapshot;
                int hops = 0;
                while(is_offset_ptr_marked(cs_fifo_mpmc_config,&next) 
                      && iterator_snapshot != NULL
                      && iterator_snapshot != tail_snapshot
                      && cs_fifo_mpmc_offset_ptr_get(&fifo->head) == head_snapshot
                     ){
                         cs_fifo_mpmc_offset_ptr_clone(&next,&iterator);
                         iterator_snapshot = cs_fifo_mpmc_offset_ptr_get(&iterator);
                         cs_fifo_mpmc_offset_ptr_clone(&iterator_snapshot->next,&next);
                         next_snapshot = cs_fifo_mpmc_offset_ptr_get(&next);
                         ++hops;
                     }

                if(cs_fifo_mpmc_offset_ptr_get(&fifo->head) != head_snapshot)
                    continue;
                else if(iterator_snapshot == tail_snapshot){
                    ck_shm_fifo_mpmc_free_chain(fifo, head_snapshot, iterator_snapshot, free_fun, free_fun_data); 
                }
                else{
                    nn = cs_fifo_mpmc_offset_ptr_get(&next);
                    *value= nn;
                    in = cs_fifo_mpmc_offset_ptr_get(&iterator);
                    cs_fifo_mpmc_offset_ptr_clone(&next,&new_next); 
                    set_offset_ptr_marked(cs_fifo_mpmc_config,&new_next,true);
                    if(cs_fifo_mpmc_offset_ptr_cas(&in->next,&next,&new_next)){
                        if(hops >= MAX_HOPS)
                            ck_shm_fifo_mpmc_free_chain(fifo, head_snapshot, next_snapshot, free_fun, free_fun_data); 

                        return true;
                    }
                }
            }
        }
    }
    return false;
}

CK_CC_INLINE static bool
ck_shm_fifo_mpmc_trydequeue(struct ck_shm_fifo_mpmc *fifo,
		    struct ck_shm_fifo_mpmc_entry **value, free_node_fun free_fun, const void * free_fun_data)
{
    cs_fifo_mpmc_offset_ptr head = CK_OFFSET_PTR_ENTRY_NULL, tail = CK_OFFSET_PTR_ENTRY_NULL, next = CK_OFFSET_PTR_ENTRY_NULL, 
    iterator = CK_OFFSET_PTR_ENTRY_NULL, new_next = CK_OFFSET_PTR_ENTRY_NULL;
    struct ck_shm_fifo_mpmc_entry * next_snapshot, * head_snapshot, * tail_snapshot, *iterator_snapshot, * nn, * in;

    cs_fifo_mpmc_offset_ptr_clone(&fifo->head,&head);
    ck_pr_fence_load();
    cs_fifo_mpmc_offset_ptr_clone(&fifo->tail,&tail);
    ck_pr_fence_load();
    head_snapshot = cs_fifo_mpmc_offset_ptr_get(&head);
    cs_fifo_mpmc_offset_ptr_clone(&head_snapshot->next,&next);

    tail_snapshot = cs_fifo_mpmc_offset_ptr_get(&tail);

    if(cs_fifo_mpmc_offset_ptr_get(&fifo->head) == head_snapshot){

        cs_fifo_mpmc_offset_ptr_clone(&head_snapshot->next,&next);
        ck_pr_fence_load();
        next_snapshot = cs_fifo_mpmc_offset_ptr_get(&next);
        if (head_snapshot == tail_snapshot){
            if(next_snapshot == NULL)
                return false;
            ck_shm_fifo_mpmc_try_fix_fail(fifo, next_snapshot, tail_snapshot);
        }
        else{
            cs_fifo_mpmc_offset_ptr_clone(&head,&iterator);
            iterator_snapshot = head_snapshot;
            int hops = 0;
            while(is_offset_ptr_marked(cs_fifo_mpmc_config,&next) 
                  && iterator_snapshot != tail_snapshot
                  && cs_fifo_mpmc_offset_ptr_get(&fifo->head) == head_snapshot
                 ){
                     cs_fifo_mpmc_offset_ptr_clone(&next,&iterator);
                     iterator_snapshot = cs_fifo_mpmc_offset_ptr_get(&iterator);
                     cs_fifo_mpmc_offset_ptr_clone(&iterator_snapshot->next,&next);
                     next_snapshot = cs_fifo_mpmc_offset_ptr_get(&next);
                     ++hops;
                 }

            if(cs_fifo_mpmc_offset_ptr_get(&fifo->head) != head_snapshot)
                return false;
            else if(iterator_snapshot == tail_snapshot){
                ck_shm_fifo_mpmc_free_chain(fifo, head_snapshot, iterator_snapshot, free_fun, free_fun_data); 
            }
            else{
                nn = cs_fifo_mpmc_offset_ptr_get(&next);
                in = cs_fifo_mpmc_offset_ptr_get(&iterator);
                cs_fifo_mpmc_offset_ptr_clone(&next,&new_next); 
                set_offset_ptr_marked(cs_fifo_mpmc_config,&new_next,true);
                if(cs_fifo_mpmc_offset_ptr_cas(&in->next,&next,&new_next)){
                    *value= nn;
                    return true;
                }
            }
        }
    }
    return false;
}

#define CK_SHM_FIFO_MPMC_ISEMPTY(f) ( is_offset_ptr_null(cs_fifo_mpmc_config,&((f)->head)) )
#define CK_SHM_FIFO_MPMC_FIRST(f)   (cs_fifo_mpmc_offset_ptr_get(&cs_fifo_mpmc_offset_ptr_get(&((f)->head))->next))
#define CK_SHM_FIFO_MPMC_NEXT(m)	(cs_fifo_mpmc_offset_ptr_get(&(m)->next))
#define CK_SHM_FIFO_MPMC_FOREACH(fifo, entry)				\
	for ((entry) = CK_SHM_FIFO_MPMC_FIRST(fifo);			\
	     (entry) != NULL;						\
	     (entry) = CK_SHM_FIFO_MPMC_NEXT(entry))
#define CK_SHM_FIFO_MPMC_FOREACH_SAFE(fifo, entry, T)			\
	for ((entry) = CK_SHM_FIFO_MPMC_FIRST(fifo);			\
	     (entry) != NULL && ((T) = cs_fifo_mpmc_offset_ptr_get(&(entry)->next), 1);	\
	     (entry) = (T))

CK_CC_INLINE static size_t
ck_shm_fifo_mpmc_count(struct ck_shm_fifo_mpmc *fifo)
{
    cs_fifo_mpmc_offset_ptr next = CK_OFFSET_PTR_ENTRY_NULL;
    struct ck_shm_fifo_mpmc_entry * snapshot = CK_SHM_FIFO_MPMC_FIRST(fifo);
    cs_fifo_mpmc_offset_ptr_clone(&cs_fifo_mpmc_offset_ptr_get(&fifo->head)->next, &next);
    size_t count = 0;
    while(snapshot != NULL){
        if(false == is_offset_ptr_marked(cs_fifo_mpmc_config,&next)){
             if (++count > INT_MAX)
                break;
        }
        snapshot = cs_fifo_mpmc_offset_ptr_get(&snapshot->next);
        cs_fifo_mpmc_offset_ptr_clone(&snapshot->next, &next);
    }
    return count;
}

#endif /* CK_F_SHM_FIFO_MPMC */
#endif /* CK_F_PR_CAS_PTR_2 */

#endif /* CK_SHM_FIFO_H */
