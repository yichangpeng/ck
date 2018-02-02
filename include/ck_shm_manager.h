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

/*
*
* shm_manager:
*           shm里面自定义数据结构声明
*
*
*/

struct shm_allocator;
typedef struct shm_allocator shm_allocator_t;
CK_OFFSET_DEFINE(shm_allocator_t)

struct shm_manager_info{
    struct ck_shm_slist_entry _list_entry;
    void_ptr                  _impl;
    char                      _name[1];
};

struct shm_manager{
    shm_allocator_t_ptr _a;
    struct ck_shm_slist _slist;
};

typedef struct shm_manager shm_manager_t;

typedef void (*create_op_parm1)(struct shm_manager *, void_ptr *);
typedef void (*create_op_parm2)(struct shm_manager *, void_ptr *, size_t );

CK_CC_INLINE static bool 
shm_manager_op(ck_shm_slist_entry_t * n, const void * data)
{
    struct shm_manager_info * ninfo = (struct shm_manager_info *)n;
    return (strcmp(ninfo->_name, (const char*)data));
}

CK_CC_INLINE void *
get_container_parm1(struct shm_manager * sm, const char * name, bool create_if_not_exist, create_op_parm1 create_op);

CK_CC_INLINE void *
get_container_parm2(struct shm_manager * sm, const char * name, bool create_if_not_exist, size_t initialize_size, create_op_parm2 create_op);

CK_CC_INLINE ck_shm_stack_t *
get_stack(struct shm_manager * sm, const char * name, bool create_if_not_exist);

CK_CC_INLINE void *
get_custom_object(struct shm_manager * sm, const char * name, size_t initialize_size, bool create_if_not_exist);

CK_OFFSET_DEFINE(shm_manager_t)

#endif
