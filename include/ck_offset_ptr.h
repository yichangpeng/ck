/*************************************************************************
    > File Name: ck_offset_ptr.h
    > Author: 
    > Mail: 
    > Created Time: Wed 03 Jan 2018 10:10:47 AM CST
 ************************************************************************/

#ifndef _CK_OFFSET_PTR_H
#define _CK_OFFSET_PTR_H

#include <assert.h>
#include <ck_pr.h>
#include <ck_stdbool.h>

#define CK_OFFSET_PTR_NULL (size_t)1
#define CK_OFFSET_PTR_ENTRY_NULL { 1 }
#define is_offset_ptr_null(config,op) ((((op)->offset_data) & config.OFFSET_BITS) > 0)
#define is_offset_ptr_marked(config,op) (((op)->offset_data & config.MARK_BIT) > 0)
#define set_offset_ptr_marked(config,op,m) (op)->offset_data = (((op)->offset_data + config.VER_STEP) & ~config.MARK_BIT) | (m?config.MARK_BIT:0)
#define is_offset_ptr_flaged(config,op) (((op)->offset_data & config.FALG_BIT) == 1)
#define set_offset_ptr_flaged(config,op,m) (op)->offset_data = (((op)->offset_data + config.VER_STEP) & ~config.FALG_BIT) | (m?config.FALG_BIT:0)
#define LINK(M,T,reserve,offset) M##_##T##_##reserve##_##offset
#define data_from_other_func(T,reserve,offset) LINK(data_from_other,T,reserve,offset)
#define data_from_other2_func(T,reserve,offset) LINK(data_from_other2,T,reserve,offset)
#define ptr_to_offset_func(T,reserve,offset) LINK(ptr_to_offset,T,reserve,offset)
#define offset_ptr_config_struct(T,reserve,offset) LINK(offset_ptr_config,T,reserve,offset)
#define offset_ptr_struct(T,reserve,offset) LINK(offset_ptr,T,reserve,offset)

#define ck_offset_ptr(config_name,offset_ptr_type_name,transfer_func,get_func,set_func,cas_func,T,reserve,offset)  \
        static struct offset_ptr_config_struct(T,reserve,offset) { \
            size_t MAX_POINTER_VALUE;   \
            size_t OFFSET_BITS;         \
            size_t MARK_BIT;            \
            size_t FLAG_BIT;            \
            size_t VER_BITS_OFFSET;     \
            size_t VER_STEP;            \
            size_t VER_BITS;            \
            size_t reserve_BITS_OFFSET; \
            size_t reserve_BITS;        \
        } config_name = \
        {                                                             \
            0xFFFFFFFFFFFFFFFF >> (64 - offset),                      \
            0xFFFFFFFFFFFFFFFF >> (64 - offset),                      \
            1ul << offset,                                            \
            1ul << (offset + 1),                                      \
            (offset + 2) + reserve,                                   \
            1ul << ((offset + 2) + reserve),                          \
            0xFFFFFFFFFFFFFFFF << ((offset + 2) + reserve),           \
            offset + 2,                                               \
            (0xFFFFFFFFFFFFFFFF << (offset + 2)) ^ (0xFFFFFFFFFFFFFFFF << ((offset + 2) + reserve)) \
         }; \
        \
        struct offset_ptr_struct(T,reserve,offset) {  \
          size_t offset_data;   \
        };  \
        \
        typedef struct offset_ptr_struct(T,reserve,offset) offset_ptr_type_name; \
        \
        \
CK_CC_INLINE static size_t \
ptr_to_offset_func(T,reserve,offset)(const volatile void * ptr, const volatile void * this_ptr){ \
     return  ptr ? (((size_t) ptr - (size_t) this_ptr) & config_name.OFFSET_BITS) : 1;; \
} \
            \
            \
CK_CC_INLINE static size_t \
data_from_other_func(T,reserve,offset)(offset_ptr_type_name *old_offset_ptr, offset_ptr_type_name *new_offset_ptr){\
    const size_t pptr = (size_t)new_offset_ptr; \
    const size_t pthis = (size_t)old_offset_ptr; \
    const size_t ptr_offset = new_offset_ptr->offset_data & config_name.OFFSET_BITS; \
    size_t new_offset = ((pptr-pthis) & (size_t)-(ptr_offset != 1)) + ptr_offset; \
    new_offset = ((new_offset & config_name.OFFSET_BITS) | (new_offset_ptr->offset_data & ~(config_name.OFFSET_BITS))); \
    return new_offset; \
} \
            \
            \
CK_CC_INLINE static size_t \
data_from_other2_func(T,reserve,offset)(offset_ptr_type_name *old_offset_ptr, offset_ptr_type_name *new_offset_ptr, size_t for_ver){\
    const size_t pthis = (size_t)old_offset_ptr; \
    const size_t pptr = (size_t)new_offset_ptr; \
    const size_t ptr_offset = new_offset_ptr->offset_data & config_name.OFFSET_BITS; \
    size_t new_offset = ((pptr-pthis) & (size_t)-(ptr_offset != 1)) + ptr_offset; \
    new_offset = ((new_offset & config_name.OFFSET_BITS) | (new_offset_ptr->offset_data & ~(config_name.OFFSET_BITS|config_name.VER_BITS) | (for_ver & config_name.VER_BITS))); \
    return new_offset;  \
} \
            \
            \
CK_CC_INLINE static void \
set_func(offset_ptr_type_name* offset_ptr, T* ptr, bool marked, bool flaged) \
{ \
    size_t pptroffset = ptr_to_offset_func(T,reserve,offset)(ptr,offset_ptr); \
    assert(pptroffset <= config_name.MAX_POINTER_VALUE); \
    offset_ptr->offset_data = pptroffset | (marked>0 ? config_name.MARK_BIT : 0) \
    | (flaged>0 ? config_name.FLAG_BIT : 0) | ((offset_ptr->offset_data & config_name.VER_BITS) + config_name.VER_STEP); \
}\
    \
    \
    \
CK_CC_INLINE static T* \
get_func(offset_ptr_type_name *offset_ptr){ \
    return ((offset_ptr->offset_data & config_name.OFFSET_BITS)==1?NULL: \
            (T*)(((size_t)offset_ptr+(offset_ptr)->offset_data & config_name.OFFSET_BITS)&config_name.MAX_POINTER_VALUE)); \
}\
    \
    \
    \
CK_CC_INLINE static bool \
cas_func(offset_ptr_type_name *old_offset_ptr, offset_ptr_type_name *compare_offset_ptr, offset_ptr_type_name *new_offset_ptr) \
{ \
    size_t compare_data = data_from_other_func(T,reserve,offset)(old_offset_ptr,compare_offset_ptr); \
    size_t new_data = data_from_other2_func(T,reserve,offset)(old_offset_ptr,new_offset_ptr,compare_offset_ptr->offset_data) + config_name.VER_STEP; \
    return ck_pr_cas_64(&old_offset_ptr->offset_data, compare_data, new_data);\
}\
    \
    \
    \
CK_CC_INLINE static void \
transfer_func(offset_ptr_type_name *old_offset_ptr, offset_ptr_type_name *new_offset_ptr) \
{ \
    new_offset_ptr->offset_data = data_from_other_func(T,reserve,offset)(new_offset_ptr,old_offset_ptr); \
}\
    \
    \
    \
CK_CC_INLINE static bool \
cas_func##_##ptr(offset_ptr_type_name *old_offset_ptr, T *compare_ptr, T *new_ptr)  \
{ \
    size_t compare_data = ptr_to_offset_func(T,reserve,offset)(compare_ptr,old_offset_ptr); \
    size_t new_data = ptr_to_offset_func(T,reserve,offset)(new_ptr,old_offset_ptr); \
    compare_data = compare_data | (old_offset_ptr->offset_data & ~config_name.OFFSET_BITS); \
    new_data = new_data | (old_offset_ptr->offset_data & ~config_name.OFFSET_BITS); \
    return ck_pr_cas_64(&old_offset_ptr->offset_data, compare_data, new_data); \
}\
   \
   \
   \
   
#endif
