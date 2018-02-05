/*
 * Copyright 2012-2015 Samy Al Bahra.
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

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)queue.h	8.5 (Berkeley) 8/20/94
 * $FreeBSD: release/9.0.0/sys/sys/queue.h 221843 2011-05-13 15:49:23Z mdf $
 */

#ifndef CK_SHM_QUEUE_H
#define	CK_SHM_QUEUE_H

#include <ck_pr.h>
#include <ck_offset_ptr.h>

/*
 * Singly-linked List declarations.
 */
struct ck_shm_slist_entry;
typedef struct ck_shm_slist_entry ck_shm_slist_entry_t;

 CK_OFFSET_DEFINE(ck_shm_slist_entry_t)

 struct ck_shm_slist_entry{
     ck_shm_slist_entry_t_ptr sle_next; 
 };

struct ck_shm_slist{
	ck_shm_slist_entry_t     slh_first;
};

#define CK_SHM_SLIST_INIT(slist)                   \
    ((slist)->slh_first.sle_next.offset_data = CK_OFFSET_PTR_NULL;)
#define	CK_SHM_SLIST_NEXT(elm)						\
	(ck_shm_slist_entry_t_ptr_get(&((elm)->sle_next)))

CK_CC_INLINE static bool 
ck_shm_slist_insert_after(ck_shm_slist_entry_t * a, ck_shm_slist_entry_t * b) 
{	
    ck_shm_slist_entry_t_ptr a_next;
    ck_shm_slist_entry_t_ptr_clone(&a->sle_next, &a_next);
	ck_pr_fence_store();							
    if (is_offset_ptr_marked(ck_shm_slist_entry_t_config,&a->sle_next)) 
        return false;
    ck_shm_slist_entry_t_ptr_clone(&a_next, &b->sle_next);
    return ck_shm_slist_entry_t_ptr_cas(&a->sle_next, &a_next, &b->sle_next);
} 

CK_CC_INLINE static void 
ck_shm_slist_insert_head(struct ck_shm_slist* slist, ck_shm_slist_entry_t * n)
{
    while (!ck_shm_slist_insert_after(&slist->slh_first, n)){};
}

CK_CC_INLINE static bool 
ck_shm_slist_try_physical_erase(ck_shm_slist_entry_t * a, ck_shm_slist_entry_t * b)
{
    ck_shm_slist_entry_t_ptr a_next;
    ck_shm_slist_entry_t_ptr_clone(&a->sle_next, &a_next);
    if (is_offset_ptr_marked(ck_shm_slist_entry_t_config,&a->sle_next)
       || CK_SHM_SLIST_NEXT(a) != b) 
        return false;
    return ck_shm_slist_entry_t_ptr_cas(&a->sle_next, &a_next, &b->sle_next);
}

struct shm_slist_pair
{
    ck_shm_slist_entry_t * first;
    ck_shm_slist_entry_t * second;
};

typedef bool (*IfOp)(ck_shm_slist_entry_t * n, const void * data);

CK_CC_INLINE static struct shm_slist_pair 
ck_shm_slist_search_if(ck_shm_slist_entry_t * first, IfOp op, const void * data, bool access_marked)
{
    ck_shm_slist_entry_t * t = first;
    ck_shm_slist_entry_t * left_node = first;
    ck_shm_slist_entry_t_ptr t_next, left_node_next;
    ck_shm_slist_entry_t_ptr_clone(&t->sle_next, &t_next);
    ck_shm_slist_entry_t_ptr_clone(&t->sle_next, &left_node_next);
    bool flag = false;
    for (; ;){
        t = ck_shm_slist_entry_t_ptr_get(&t_next);
        if (t == NULL){
            if (flag){
                ck_shm_slist_entry_t_ptr temp = CK_OFFSET_PTR_ENTRY_NULL;
                ck_shm_slist_entry_t_ptr_cas(&left_node->sle_next, &left_node_next, &temp);
            }
            t = NULL;

            struct shm_slist_pair pair = {left_node, t};
            return pair;
        }
        t = ck_shm_slist_entry_t_ptr_get(&t_next);
        ck_shm_slist_entry_t_ptr_clone(&t->sle_next, &t_next);

        if (is_offset_ptr_marked(ck_shm_slist_entry_t_config,&t_next)
            && op(t,data))
        {
            flag = true;
            if (access_marked && op(t, data)){
                struct shm_slist_pair pair = {left_node, t};
                return pair;
            }
        }
        else{
            if (flag){
                ck_shm_slist_entry_t_ptr pt;
                ck_shm_slist_entry_t_ptr_set(&pt, t, false, false);
                ck_shm_slist_entry_t_ptr_cas(&left_node->sle_next, &left_node_next, &pt);
                flag = false;
            }

            if (op(t, data))
            {
                struct shm_slist_pair pair = {left_node, t};
                return pair;
            }
            left_node = t;
            ck_shm_slist_entry_t_ptr_clone(&t_next, &left_node_next);
        }
    }
}

CK_CC_INLINE static bool 
find_prev_op(ck_shm_slist_entry_t * n, const void * data)
{
    return n == data;
}

CK_CC_INLINE static void 
ck_shm_slist_physical_erase(ck_shm_slist_entry_t * first, ck_shm_slist_entry_t * n)
{
    for (; ;){
        struct shm_slist_pair pair = ck_shm_slist_search_if(first, find_prev_op, n, true);         
        if (pair.second == NULL)
            break;

        ck_shm_slist_entry_t * left_node = pair.first;
        ck_shm_slist_entry_t_ptr left_node_next;
        ck_shm_slist_entry_t_ptr_clone(&left_node->sle_next, &left_node_next);

        if (is_offset_ptr_marked(ck_shm_slist_entry_t_config,&left_node_next))
            continue;

        ck_shm_slist_entry_t_ptr n_next;
        ck_shm_slist_entry_t_ptr_clone(&n->sle_next, &n_next);
        if (ck_shm_slist_entry_t_ptr_cas(&left_node->sle_next, &left_node_next, &n_next))
            break;
    }
}

CK_CC_INLINE static bool 
ck_shm_slist_mark_node(ck_shm_slist_entry_t * n)
{
    for (; ;){
        ck_shm_slist_entry_t_ptr old_next;
        ck_shm_slist_entry_t_ptr_clone(&n->sle_next, &old_next);
        if (is_offset_ptr_marked(ck_shm_slist_entry_t_config,&old_next))
            return false;
        ck_shm_slist_entry_t_ptr new_next;
        ck_shm_slist_entry_t_ptr_clone(&old_next, &new_next);
        set_offset_ptr_marked(ck_shm_slist_entry_t_config, &new_next, true);
        if (ck_shm_slist_entry_t_ptr_cas(&n->sle_next, &old_next, &new_next))
            return true;
    }
}

CK_CC_INLINE static ck_shm_slist_entry_t *
ck_shm_slist_pop_front(struct ck_shm_slist * slist)
{
    for (; ;){
        ck_shm_slist_entry_t * t = CK_SHM_SLIST_NEXT(&slist->slh_first);
        if ( t == NULL )
            return NULL;
        if ( ck_shm_slist_mark_node(t) ){
            ck_shm_slist_physical_erase(&slist->slh_first, t);
            return t;
        }
        ck_shm_slist_try_physical_erase(&slist->slh_first, t);
    }
}

CK_CC_INLINE static bool 
ck_shm_slist_insert_middle(ck_shm_slist_entry_t * a, 
    ck_shm_slist_entry_t * b, ck_shm_slist_entry_t * n)
{
    ck_shm_slist_entry_t_ptr a_next;            
    ck_shm_slist_entry_t_ptr_clone(&a->sle_next, &a_next);

    ck_shm_slist_entry_t * anext_snapshot = ck_shm_slist_entry_t_ptr_get(&a_next);
    if (is_offset_ptr_marked(ck_shm_slist_entry_t_config,&a_next) 
       || anext_snapshot != b)
        return false;
    ck_shm_slist_entry_t_ptr_set(&n->sle_next, b, false, false);
    return ck_shm_slist_entry_t_ptr_cas_ptr(&a->sle_next, anext_snapshot, n);
}

#endif
