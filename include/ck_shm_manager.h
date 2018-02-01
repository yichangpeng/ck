/*************************************************************************
	> File Name: ck_shm_manager.h
	> Author: 
	> Mail: 
	> Created Time: Wed 31 Jan 2018 05:18:19 PM CST
 ************************************************************************/

#ifndef _CK_SHM_MANAGER_H
#define _CK_SHM_MANAGER_H

#include <ck_shm_queue.h>
#include <ck_shm_stack.h>
#include <ck_shm_allocator.h>

struct shm_allocator;

/*
*
* shm_manager:
*           shm里面自定义数据结构声明
*
*
*/

struct shm_manager_info{
    struct ck_shm_slist_entry _list_entry;
    void_ptr                  _impl;
    char                      _name[1];
};

typedef struct ck_shm_slist shm_manager_t;

typedef void (*create_op_parm1)(struct shm_allocator *, void_ptr *);
typedef void (*create_op_parm2)(struct shm_allocator *, void_ptr *, size_t );

CK_CC_INLINE static bool 
shm_manager_op(ck_shm_slist_entry_t * n, const void * data)
{
    struct shm_manager_info * ninfo = (struct shm_manager_info *)n;
    return (strcmp(ninfo->_name, (const char*)data));
}

CK_CC_INLINE void  
create_stack_container(struct shm_allocator * allocator, void_ptr * container); 

CK_CC_INLINE void *
get_container_parm1(struct shm_allocator * allocator, const char * name, bool create_if_not_exist, create_op_parm1 create_op);

CK_CC_INLINE void *
get_container_parm2(struct shm_allocator * allocator, const char * name, bool create_if_not_exist, size_t initialize_size, create_op_parm2 create_op);

CK_CC_INLINE ck_shm_stack_t *
get_stack(struct shm_allocator * allocator, const char * name, bool create_if_not_exist);

CK_OFFSET_DEFINE(shm_manager_t)

#endif
