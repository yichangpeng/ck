/*************************************************************************
    > File Name: ck_shm_allocator.h
    > Author: 
    > Mail: 
    > Created Time: Tue 12 Dec 2017 03:01:33 PM CST
 ************************************************************************/

#ifndef _CK_SHM_ALLOCATOR_H
#define _CK_SHM_ALLOCATOR_H

#include <stdint.h>
#include <assert.h>
#include <memory.h> 
#include <stdbool.h>
#include <ck_pr.h>
#include <ck_stack.h>
#include <ck_queue.h>

CK_CC_INLINE static uint16_t 
now_unit(void)
{
#if ( defined( __i386__  ) || defined( __x86_64__  ))
     uint32_t low, high;
        __asm__ __volatile__("rdtsc" : "=a" (low), "=d" (high));
     const int bits = 1;// 若cpu为2G，则相当于一秒
     return (high << bits) | (low >> (32 - bits));
#else
     return (uint16_t) ::time(NULL);
#endif
}

#define small_bin_count 48
static const size_t     default_block_size = 8192;
static const size_t     max_alloc_size = 48*8;
static const uint16_t   DEFAULT_DELAY_UNIT = 8;

static const size_t     CINUSE_BIT = 0x01;
static const size_t     PINUSE_BIT = 0x02;
static const size_t     WAIT_MERGE = 0x04;
static const size_t     HEAD_SIZE = sizeof(uint64_t);
static const size_t     SPACE_OFFSET = sizeof(uint64_t);
static const uint8_t    FLAGS_DELAY_BIT= 0x01;
static const uint8_t    NEVER_FREE_BIT= 0x02;
static const uint8_t    MERGING_BIT= 0x04;
static const size_t     INUSE_BITS = 7 | (((size_t) 0xFFFFFF) << 40);
static const size_t     MAX_SIZE = (((size_t) 1) << 40) - 1;// 最大支持1T

static const uint8_t    SHM_TYPE_UNKNOWN= 0;
static const uint8_t    SHM_TYPE_POSIX= 1;
static const uint8_t    SHM_TYPE_SYSV= 2;
static const uint8_t    SHM_TYPE_MMAP= 3;

/*
*qnode_helper:
*      将32位数值指针转化为联合体,低位n1表示其值，高位n2表示指针的低32位
*       在for循环成功n1+1,下次循环时n2位n1+1的值,n1为n1+1,说明操作成功
*/
typedef union qnode_helper_union
{
    struct {
        uint32_t n1;
        uint32_t n2;
    } in_qnode;
    uint64_t n64;
#define n1 in_qnode.n1
#define n2 in_qnode.n2
}qnode_helper;

CK_CC_INLINE static void 
helper_setdata(qnode_helper * h, volatile void * ptr){
    h->n64 = *((volatile uint64_t *) ptr);
}

/*
*qnode_ptr:
*     节点信息 
*       
*/
typedef union qnode_ptr_union{
    struct
    {
        volatile uint32_t   _offset_low;
        volatile uint8_t    _offset_high;
        volatile uint8_t    _size_d8;          ///< 块大小/8，由于块按8字节对齐，因此把低三位利用起来，以表示更大的块，最大允许块大小为256*8
        volatile uint16_t   _expire_unit;
    }in_qnode_ptr;
    volatile uint64_t       _data;
#define _offset_low  in_qnode_ptr._offset_low
#define _offset_high in_qnode_ptr._offset_high 
#define _size_d8     in_qnode_ptr._size_d8
#define _expire_unit in_qnode_ptr._expire_unit
}qnode_ptr;

CK_CC_INLINE static size_t 
qnode_ptr_get_offset(qnode_ptr * qp) {
    return qp->_data & 0x000000FFFFFFFFFF;
}

CK_CC_INLINE static size_t 
qnode_ptr_get_size(qnode_ptr * qp) {
    return (size_t)qp->_size_d8 << 3;
}

CK_CC_INLINE static void *
qnode_ptr_get_ptr(qnode_ptr * qp, char * base) {
    return (void*)(base + qnode_ptr_get_offset(qp));
}

CK_CC_INLINE static void 
qnode_ptr_set(qnode_ptr * qp, size_t ptr, size_t n, uint16_t expire_unit){
    qp->_size_d8 = n >> 3;
    qp->_expire_unit = expire_unit;
    qp->_offset_high = ptr >> 32;
    qp->_offset_low = ptr;
}
/*
*
*   shm_alloc_chunk:
*        分配的基本单位
*
*
*/
struct shm_alloc_chunk{
    union
    {
        struct
        {
            volatile uint32_t   _head_low;
            volatile uint8_t    _head_high;
            volatile uint8_t    _flags;
            volatile uint16_t   _chunk_expire_unit;
        } in_chunk;
        volatile size_t _chunk_head;
        volatile uint64_t _data;
     } chunk_un;
#define _head_low             chunk_un.in_chunk._head_low
#define _head_high            chunk_un.in_chunk._head_high 
#define _flags                chunk_un.in_chunk._flags
#define _chunk_expire_unit    chunk_un.in_chunk._chunk_expire_unit
#define _chunk_head           chunk_un._chunk_head
#define _data                 chunk_un._data
};
typedef struct shm_alloc_chunk shm_alloc_chunk_t;

CK_CC_INLINE static size_t 
shm_alloc_chunk_getsize(shm_alloc_chunk_t * c)
{
    return c->_chunk_head & ~INUSE_BITS;
}

CK_CC_INLINE static shm_alloc_chunk_t * 
shm_alloc_chunk_nextchunk(shm_alloc_chunk_t * c)
{
    return ((shm_alloc_chunk_t *) (((char*) (c)) + (c->_chunk_head & ~INUSE_BITS)));
}

CK_CC_INLINE static void * 
shm_alloc_chunk_getspace(shm_alloc_chunk_t * c)
{
    return ((char *) c) + SPACE_OFFSET;
}

CK_CC_INLINE static  shm_alloc_chunk_t *
shm_alloc_chunk_getchunk(void * p)
{
    return (shm_alloc_chunk_t *) ((char *) p - SPACE_OFFSET);
}

CK_CC_INLINE static bool
shm_alloc_chunk_cas(shm_alloc_chunk_t * origin, shm_alloc_chunk_t * ox, size_t head, uint16_t expire_util, uint8_t flags)
{
    shm_alloc_chunk_t nx = *ox;
    nx._chunk_head = head;
    nx._chunk_expire_unit = expire_util;
    nx._flags = flags;
    return ck_pr_cas_ptr(origin, ox, &nx);
}

/*
*
* shm_chunk_ptr:
*           大内存分配chunk信息
*
*
*/
typedef union shm_chunk_ptr_union{
    struct
    {
        volatile uint32_t   _offset_low_;
        volatile uint8_t    _offset_high_;
        volatile uint8_t    _loop_count_;
        volatile uint16_t   _ver_;
    } in_ptr;
    volatile uint64_t       _data_;
#define _offset_low_ in_ptr._offset_low_
#define _offset_high_ in_ptr._offset_high_
#define _loop_count_ in_ptr._loop_count_
#define _ver_ in_ptr._ver_
}shm_chunk_ptr;

CK_CC_INLINE static size_t 
shm_chunk_ptr_offset(shm_chunk_ptr * ptr) {
    return ptr->_data_ & 0x000000FFFFFFFFFF;
}

CK_CC_INLINE static void 
shm_chunk_ptr_set(shm_chunk_ptr * ptr, size_t pointer, size_t loop_count, size_t ver)
{
    ptr->_loop_count_ = loop_count;
    ptr->_ver_ = ver;
    ptr->_offset_high_ = pointer >> 32;
    ptr->_offset_low_ = pointer;
}

CK_CC_INLINE static bool 
shm_chunk_ptr_cas(shm_chunk_ptr* origin, shm_chunk_ptr* ox, size_t offset, bool restart)
{
    shm_chunk_ptr nx;
    shm_chunk_ptr_set(&nx, offset, ox->_loop_count_ + restart, ox->_ver_ );
    return ck_pr_cas_ptr(origin, ox, &nx);
}

CK_CC_INLINE static bool 
shm_chunk_is_finish(const shm_chunk_ptr* self, const shm_chunk_ptr* old)
{
    uint8_t sub_loop_count = self->_loop_count_ - old->_loop_count_;
    return sub_loop_count > 1;
}

struct shm_small_alloc_impl;
typedef struct shm_small_alloc_impl shm_small_alloc_impl_t;
struct shm_allocator;
typedef struct shm_allocator shm_allocator_t;
struct delay_queue_impl;
typedef struct delay_queue_impl delay_queue_impl_t;
struct shm_large_alloc_impl;
typedef struct shm_large_alloc_impl shm_large_alloc_impl_t;
struct shm_manager;
typedef struct shm_manager shm_manager_t;

/*
*
* 延迟队列:
*           实现方式同ringbuffer(循环队列),超时时间只取实际时间秒数16位
*   
*
*/
struct delay_queue_impl{
    uint32_t  _head;
    uint32_t  _tail;
    uint32_t  _push_tail;
    uint32_t  _push_count;
    size_t    _max_size;
    qnode_ptr _v[1];
};

/*
*
*   shm_small_alloc_impl:
*           小内存分配器:类似stl的allocator,分级分配,但是延迟释放
*
*
*/
struct shm_small_alloc_impl
{
    shm_allocator_t*    _allocator;
    delay_queue_impl_t  _delay_q;
    int                 _small_bin_total[small_bin_count];
    ck_stack_t          _small_bin[small_bin_count];
};

/*
*
*   shm_large_alloc_impl:
*           大内存分配器:类似stl的第一级allocator,一直向后分配直到缓存区尾部
*           再重新来分配空间并尝试合并之前已经释放的空间
*
*/

struct shm_large_alloc_impl{
    shm_chunk_ptr       _current_chunk_ptr;
    shm_allocator_t*    _allocator;
};

/*
*
* shm_manager:
*           shm里面自定义数据结构声明
*
*
*/

struct shm_manager_info{
    void* _impl;
    char  _name[1];
};

CK_SLIST_HEAD(shm_manager,shm_manager_info);

/*
*
* shm_allocator:
*           shm内存分配器,大内存由一个链表组成;小内存分级分配,延迟释放
*
*
*/
struct shm_allocator
{
    uint16_t                _version;
    uint8_t                 _shm_type;
    uint8_t                 _reserve[3];
    size_t                  _buffer_length;
    size_t                  _max_alloc_size;
    size_t                  _max_chunk_size;
    size_t                  _process_info;
    void*                   _custom_data_ptr;
    size_t                  _reserve_field[4];
    size_t                  _head_size;
    shm_large_alloc_impl_t  _large_alloc_impl;
    shm_manager_t           _shm_manager;
    shm_small_alloc_impl_t  *_small_alloc_impl;
};

CK_CC_INLINE static void *
alloc_large(shm_large_alloc_impl_t * la, size_t n, size_t aligned_size, uint8_t add_chunk_flags);

CK_CC_INLINE static bool 
allow_aligned_size(size_t aligned_size)
{
    return (aligned_size & (aligned_size - 1)) == 0 && aligned_size >= 8;
}


CK_CC_INLINE static void
delay_queue_init(delay_queue_impl_t * delay_queue, size_t max_size)
{
    delay_queue->_head = 0;
    delay_queue->_tail = 0;
    delay_queue->_push_tail = 0;
    delay_queue->_push_count = 0;
    delay_queue->_max_size = max_size;
    memset(delay_queue->_v, 0, delay_queue->_max_size * sizeof(qnode_ptr));
}

CK_CC_INLINE static void
delay_queue_impl_check_fp(delay_queue_impl_t * delay_queue)
{
    qnode_helper h;
    helper_setdata(&h,&delay_queue->_push_tail);
    uint32_t pt = h.n1;
    //由于_for_push的head比tail先增加，因此当且仅当head == tail时，_for_push之前的push操作都已经完成
    if(pt == h.n2){
        for(; ;){
            uint32_t t = delay_queue->_tail;
            if(pt <= t)
                return;
            if(ck_pr_cas_32(&delay_queue->_tail,t,pt))
                return;
        }
    }
}

typedef void (*Callback)(shm_small_alloc_impl_t *, qnode_ptr *);

CK_CC_INLINE static void
delay_queue_impl_push(shm_small_alloc_impl_t * sa, delay_queue_impl_t * delay_queue, const char * base, char * p, size_t n, uint16_t expire_unit, Callback cb)
{
    qnode_ptr qp;
    qnode_ptr_set(&qp,p-base,n,expire_unit);
    for(;;){
        delay_queue_impl_check_fp(delay_queue);
        
        uint32_t pt = delay_queue->_push_tail;
        if(pt - delay_queue->_head >= delay_queue->_max_size)
        {
            //pop
            qnode_helper h;
            helper_setdata(&h,&delay_queue->_head);
            if(h.n1 == h.n2)
                continue;

            uint32_t new_h = h.n1 + 1;
            qnode_ptr pp = delay_queue->_v[h.n1 % delay_queue->_max_size];
            if(ck_pr_cas_32(&delay_queue->_head, h.n1, new_h)){
                cb(sa,&pp);
            }
            continue;
        }
        
        uint32_t pt_new = pt + 1;
        if(ck_pr_cas_32(&delay_queue->_push_tail, pt, pt_new)){
            delay_queue->_v[pt % delay_queue->_max_size] = qp;
            ck_pr_faa_32(&delay_queue->_push_count, (uint32_t)1);
            ck_pr_cas_32(&delay_queue->_tail, pt, pt_new);     // 若失败，则延后到下次check_fp时后移_tail
            return;
        }
    }
}

CK_CC_INLINE static bool 
delay_free_expire(uint16_t now, uint16_t expire)
{
    // 只需保证now不在expire的前DEFAULT_DELAY_UNIT单位内，就能释放
    // 即expire - now > DEFAULT_DELAY_UNIT || expire - now < 0时能释放
    // 由于结果是无符号整型，因此：
    return (uint16_t)(expire- now)> DEFAULT_DELAY_UNIT;
}

CK_CC_INLINE static void *
pop_expired(delay_queue_impl_t * delay_queue, char* base, uint16_t now_unit, size_t *psize)
{
    for(; ;){
        delay_queue_impl_check_fp(delay_queue);

        qnode_helper h;
        helper_setdata(&h,&delay_queue->_head);

        if(h.n1 == h.n2)
            return NULL;

        uint32_t new_h = h.n1 + 1;
        qnode_ptr p = delay_queue->_v[h.n1 % delay_queue->_max_size];
        if(!delay_free_expire(now_unit, p._expire_unit))
            return NULL;
        if(ck_pr_cas_32(&delay_queue->_head, h.n1, new_h)){
            *psize = qnode_ptr_get_size(&p);
            return qnode_ptr_get_ptr(&p,base);
        }
    } 
}

CK_CC_INLINE static char*
get_buffer(shm_allocator_t * allocator){
    return (char*)allocator;
}

CK_CC_INLINE static ck_stack_t*
gs(shm_small_alloc_impl_t * sa, size_t n)
{
   return sa->_small_bin + n/8 + 1; 
}

CK_CC_INLINE static bool 
initialize_shm_small_alloc_impl(shm_allocator_t * allocator, size_t max_allocsize);

CK_CC_INLINE static int 
try_free_delay_q(shm_small_alloc_impl_t * sa)
{
    shm_allocator_t *allocator = sa->_allocator;
    char * base = get_buffer(allocator);
    uint16_t now = now_unit();
    int count = 0;
    void * p = NULL;
    size_t n = 0;
    while ((p = pop_expired(&sa->_delay_q, base, now, &n))) {
        ck_stack_t * s = gs(sa,n);
        ck_stack_push_mpmc(s,p);
        ++count;
    }
    return count;
}

CK_CC_INLINE static ck_stack_entry_t * 
alloc_from_list_inner(shm_small_alloc_impl_t * sa, ck_stack_t * s)
{
    ck_stack_entry_t * nd = ck_stack_pop_mpmc(s);
    if (nd == NULL && try_free_delay_q(sa) > 0)
        nd = ck_stack_pop_mpmc(s);
    return nd;
}

CK_CC_INLINE static void * 
alloc_and_add_to_list(shm_small_alloc_impl_t * sa, ck_stack_t * s, size_t n)
{
    shm_allocator_t *allocator = sa->_allocator;
    size_t c = default_block_size / n;
    size_t size = n * c;
    char * p = (char *) alloc_large(&allocator->_large_alloc_impl, size, 8, NEVER_FREE_BIT);
    for (size_t i = n; i < size; i += n) {
        ck_stack_entry_t * nd = (ck_stack_entry_t *) (p + i);
        ck_stack_push_mpmc(s,nd);
    }
    ck_pr_faa_int(&sa->_small_bin_total[s - sa->_small_bin], (int) c);
    return p;
}

CK_CC_INLINE static ck_stack_entry_t * 
alloc_from_list(shm_small_alloc_impl_t * sa, size_t n)
{
    ck_stack_t * s = gs(sa,n);
    ck_stack_entry_t * nd = alloc_from_list_inner(sa, s);
    if (nd != NULL)
        return nd;
    //TODO:待优化
    if (n <= 64) {
        nd = alloc_from_list(sa, n * 2);
        if (nd != NULL) {
            ck_stack_entry_t * nd2 = (ck_stack_entry_t *)(((char *) nd) + n);
            ck_stack_push_mpmc(s,nd2);
            ck_pr_faa_int(&sa->_small_bin_total[s - sa->_small_bin], 2);
            ck_pr_faa_int(&sa->_small_bin_total[gs(sa,n * 2) - sa->_small_bin], -1);
            return nd;
        }
    }
    return NULL;
}

CK_CC_INLINE size_t 
aligne_space(size_t n)
{
    const size_t aligned_size = 8;
    return (n + aligned_size - 1) & ~(aligned_size-1)
}

CK_CC_INLINE static void
shm_small_alloc_impl_init(shm_small_alloc_impl_t * sa, shm_allocator_t * allocator){
    sa->_allocator = allocator;    
}

CK_CC_INLINE static void*
alloc_small(shm_small_alloc_impl_t * sa, size_t n)
{
    n = aligne_space(n);
    assert(n <= max_alloc_size);
    void * nd = alloc_from_list(sa, n);
    if (nd)
        return nd;
    ck_stack_t * s = gs(sa,n);
    return alloc_and_add_to_list(sa, s, n);
}

CK_CC_INLINE static void
push_callback(shm_small_alloc_impl_t * sa, qnode_ptr * i)
{
    shm_allocator_t * allocator = sa->_allocator;
    char * base = get_buffer(allocator);
    ck_stack_t * s = gs(sa,qnode_ptr_get_size(i));
    ck_stack_entry_t * nd = qnode_ptr_get_ptr(i,base);
    ck_stack_push_mpmc(s,nd);
}

CK_CC_INLINE static void
free_small(shm_small_alloc_impl_t * sa, void * p, size_t n, bool delay)
{
    shm_allocator_t * allocator = sa->_allocator;
    n = aligne_space(n);
    assert(n <= max_alloc_size);
    if (delay) {
        char * base = get_buffer(allocator);
        delay_queue_impl_push(sa, &sa->_delay_q, base, (char *) p, n, now_unit() + DEFAULT_DELAY_UNIT, push_callback);
    } else {
        ck_stack_t * s = gs(sa,n);
        ck_stack_push_mpmc(s,p);
    }
}

CK_CC_INLINE static void
add_to_bins_and_fetch_total(shm_small_alloc_impl_t * sa, void * p, size_t n)
{
    shm_allocator_t * allocator = sa->_allocator;
    n = aligne_space(n);
    ck_pr_faa_int(&sa->_small_bin_total[gs(sa,n * 2) - sa->_small_bin], 1);
    char * base = get_buffer(allocator);
    delay_queue_impl_push(sa, &sa->_delay_q, base, (char *) p, n, now_unit() + DEFAULT_DELAY_UNIT, push_callback);
}

CK_CC_INLINE static shm_alloc_chunk_t * 
get_chunk(shm_allocator_t *alloc, size_t offset){
    return (shm_alloc_chunk_t *) ((char *) alloc + offset);
}

CK_CC_INLINE static shm_alloc_chunk_t * 
get_chunk_by_ptr(void * p){
    return (shm_alloc_chunk_t *) ((char *)p - SPACE_OFFSET);
}

CK_CC_INLINE static shm_alloc_chunk_t * 
get_first_chunk(shm_allocator_t * alloc)
{
     return (shm_alloc_chunk_t *) ((char *) alloc + alloc->_head_size);
}

CK_CC_INLINE static shm_alloc_chunk_t *
get_last_chunk(shm_allocator_t * alloc)
{
    return (shm_alloc_chunk_t *)((char *) alloc + alloc->_buffer_length - SPACE_OFFSET);
}

bool
initialize_shm_allocator(shm_allocator_t *alloc, size_t length, size_t max_allocsize, bool safe_init);

CK_CC_INLINE static void
delay_free_large(shm_alloc_chunk_t * c)
{
    assert(c->_chunk_head & CINUSE_BIT);
    uint16_t expire_unit = now_unit() + DEFAULT_DELAY_UNIT;
    for (; ;) {
        shm_alloc_chunk_t cd = *c;
        if (shm_alloc_chunk_cas(c, &cd, c->_chunk_head, expire_unit, FLAGS_DELAY_BIT))
            break;
    }
}

CK_CC_INLINE static void
free_large(shm_small_alloc_impl_t * sa, shm_alloc_chunk_t * c){
    (void)sa;
    (void)c;
}

CK_CC_INLINE static void * 
alloc_ex(shm_allocator_t * a, size_t n)
{
    if (n > max_alloc_size)
        return alloc_large(&a->_large_alloc_impl, n, 8, 0);
    return alloc_small(a->_small_alloc_impl, n);
}

CK_CC_INLINE static void 
free_ex(shm_allocator_t * a, void * p, size_t n, bool delay)
{
    if (p != NULL) {
        if (n <= max_alloc_size) {
            free_small(a->_small_alloc_impl, p, n, delay);
        } else if (delay) {
            delay_free_large(get_chunk_by_ptr(p));
        } else {
            free_large(a->_small_alloc_impl, get_chunk_by_ptr(p));
        }
    }
}

CK_CC_INLINE static void *
alloc_static(shm_allocator_t * a, size_t n)
{
    if (n > max_alloc_size)
        return alloc_large(&a->_large_alloc_impl, n, 8, NEVER_FREE_BIT);
    return alloc_small(a->_small_alloc_impl, n);
}

#endif
