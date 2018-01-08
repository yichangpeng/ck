/*************************************************************************
    > File Name: ck_offset_ptr.h
    > Author: 
    > Mail: 
    > Created Time: Wed 03 Jan 2018 10:10:47 AM CST
 ************************************************************************/

#ifndef _CK_OFFSET_PTR_H
#define _CK_OFFSET_PTR_H

#include "ck_stdbool.h"
#include <assert.h>

#define ck_offset_ptr(name,T,reserve_bit_count,offset_bit_count)     \
        struct offset_ptr_config_##T_##reserve_bit_count##offset_bit_count{   \
            size_t MAX_POINTER_VALUE;   \
            size_t OFFSET_BITS;         \
            size_t MARK_BIT;            \
            size_t FLAG_BIT;            \
            size_t VER_BITS_OFFSET;     \
            size_t VER_STEP;            \
            size_t VER_BITS;            \
            size_t RESERVE_BITS_OFFSET; \
            size_t RESERVE_BITS;        \
        } name =                                                                \
        {                                                                      \
            0xFFFFFFFFFFFFFFFF >> (64 - offset_bit_count),                      \
            0xFFFFFFFFFFFFFFFF >> (64 - offset_bit_count),                      \
            1ul << offset_bit_count,                                            \
            1ul << (offset_bit_count + 1),                                      \
            (offset_bit_count + 2) + reserve_bit_count,                         \
            1ul << ((offset_bit_count + 2) + reserve_bit_count),                \
            0xFFFFFFFFFFFFFFFF << ((offset_bit_count + 2) + reserve_bit_count), \
            offset_bit_count + 2,                                               \
            (0xFFFFFFFFFFFFFFFF << (offset_bit_count + 2)) ^ (0xFFFFFFFFFFFFFFFF << ((offset_bit_count + 2) + reserve_bit_count)) \
         }; \
        \
        struct offset_ptr_##T_##reserve_bit_count##offset_bit_count{            \
          volatile size_t offset_data;                                                \
        };                                                                      \
        
#define ck_offset_ptr_t(T,reserve_bit_count,offset_bit_count) struct offset_ptr_##T_##reserve_bit_count##offset_bit_count

#define ck_set_offset_ptr(config,offset_ptr, ptr, marked, flaged)                                       \
        {                                                                                               \
        char * this_ptr = (char*)offset_ptr;                                                            \
        size_t offset = ptr ? (((size_t) ptr - (size_t) this_ptr) & config.OFFSET_BITS) : 1;            \
        assert(offset <= config.MAX_POINTER_VALUE);                                                     \
        (offset_ptr)->offset_data = offset | (marked>0 ? config.MARK_BIT : 0)                                   \
        | (flaged>0 ? config.FLAG_BIT : 0) | (((offset_ptr)->offset_data & config.VER_BITS) + config.VER_STEP); \
        }\

#define ck_get_offset_ptr(config,offset_ptr)                                                      \
        ((offset_ptr)->offset_data & config.OFFSET_BITS==1?NULL:(void*)(((size_t)offset_ptr+(offset_ptr)->offset_data & config.OFFSET_BITS)&config.MAX_POINTER_VALUE))

#endif
