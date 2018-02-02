/*************************************************************************
	> File Name: ck_shm_allocator.c
	> Author: 
	> Mail: 
	> Created Time: Mon 18 Dec 2017 05:37:10 PM CST
 ************************************************************************/

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ck_malloc.h>
#include <ck_hs.h>
#include <ck_ht.h>
#include "ck_ht_hash.h"
#include <ck_shm_allocator.h>

inline static size_t 
aligned_fill_count(size_t n, size_t aligned_size)
{
    n &= aligned_size - 1;
    return n == 0 ? 0 : aligned_size - n;
}

//将填充的小块设置为空闲,使得返回的chunk和这个空闲块为algin_size的整数倍
static void * 
split_for_aligned(shm_alloc_chunk_t * c, size_t _aligned_fill_count)
{
    if (_aligned_fill_count == 0)
        return shm_alloc_chunk_getspace(c); 
    shm_alloc_chunk_t * nc = (shm_alloc_chunk_t *) ((char *) c + _aligned_fill_count);
    nc->_chunk_head = (shm_alloc_chunk_getsize(c) - _aligned_fill_count) | CINUSE_BIT;
    for (; ;) {
        shm_alloc_chunk_t cd = *c;
        if (shm_alloc_chunk_cas(c, &cd, _aligned_fill_count | (cd._chunk_head & PINUSE_BIT), 0, 0))
            break;

    }
    return shm_alloc_chunk_getspace(nc); 
}

bool
initialize_shm_small_alloc_impl(shm_allocator_t * allocator, size_t max_allocsize)
{
    assert(allocator);

    int cpu_count = 8;
#ifdef _SC_NPROCESSORS_CONF
     cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    const int max_qsize = cpu_count * 25600;
    size_t n = sizeof(shm_small_alloc_impl_t) + sizeof(qnode_ptr) * max_qsize;
    /*ck_stack_pop_mpmc要求ck_shm_stack_t的地址必须是16字节的整数倍,*/
    shm_small_alloc_impl_t * sa = alloc_large(allocator, n, 16, NEVER_FREE_BIT);
    if(sa == NULL)
        return false;

    memset(sa->_small_bin_total,0,sizeof(sa->_small_bin_total));
    sa_offset_ptr_set(&allocator->_small_alloc_impl_offset_ptr,sa,false,false);
    shm_small_alloc_impl_init(allocator);

    for(size_t i = 0; i < sizeof(sa->_small_bin)/sizeof(sa->_small_bin[0]); ++i)
    {
        ck_shm_stack_init(sa->_small_bin+i);
    }
    delay_queue_init(&sa->_delay_q, max_qsize);

    if (max_allocsize > 0 && max_allocsize < allocator->_max_alloc_size)
         allocator->_max_alloc_size = max_allocsize;
    return true;
}

static bool 
try_step_current_chunk_ptr(shm_allocator_t * la, uint16_t now, shm_chunk_ptr* old, size_t offset)
{
    shm_allocator_t *allocator = la;
    const bool restart = offset >= (allocator->_buffer_length - SPACE_OFFSET);
    if(shm_chunk_ptr_cas(&la->_current_chunk_ptr, old, restart?allocator->_head_size:offset, restart)){
        shm_alloc_chunk_t * c = get_chunk(allocator,offset);
        shm_alloc_chunk_t cd = *c;
        if((cd._flags & FLAGS_DELAY_BIT)
           && delay_free_expire(now, cd._chunk_expire_unit)
          )
        {
            if(shm_alloc_chunk_cas(c, &cd, cd._chunk_head, 0, 0))
                free_large(c);
            else{
                sched_yield();
            }
        }
        return true;
    }
    return false;
}

static bool 
try_merge_after(shm_allocator_t * allocator, shm_alloc_chunk_t * c, uint16_t now)
{
    const uint16_t expire = now + DEFAULT_DELAY_UNIT;
    for (; ;) {
        shm_alloc_chunk_t cd = *c;
        if (cd._chunk_head & (WAIT_MERGE | CINUSE_BIT))
            return false;

        if (cd._flags & MERGING_BIT) {
            if (delay_free_expire(now, cd._chunk_expire_unit)) {
                if (shm_alloc_chunk_cas(c, &cd, cd._chunk_head, 0, cd._flags & ~MERGING_BIT))
                continue;
            }
            return false;
        }

        shm_alloc_chunk_t * nc = (shm_alloc_chunk_t *) ((char *) c + shm_alloc_chunk_getsize(&cd));
        shm_alloc_chunk_t ncd = *nc;
        if (ncd._chunk_head & CINUSE_BIT) {
            // 处理延时释放
            if ((ncd._flags & FLAGS_DELAY_BIT)
                && delay_free_expire(now, ncd._chunk_expire_unit)){
                    // 修改过期时间，以免多个线程同时调用free_impl
                    // 只有成功修改的线程才能进入free_impl
                    if(shm_alloc_chunk_cas(nc, &ncd, ncd._chunk_head, 0, 0))
                        free_large(nc);
                    else{
                        sched_yield();
                    }
                    continue;
                }
            return false;
        }

        if(!shm_alloc_chunk_cas(c, &cd, cd._chunk_head, expire, cd._flags|MERGING_BIT))
            return false;

        cd._chunk_expire_unit = expire;
        cd._flags |= MERGING_BIT;

        if(!ncd._chunk_head & WAIT_MERGE){
            if(!shm_alloc_chunk_cas(c, &ncd, ncd._chunk_head|MERGING_BIT, 0, 0))
            {
                for(; ;){
                    if(!shm_alloc_chunk_cas(c, &cd, cd._chunk_head, cd._flags|MERGING_BIT, 0))
                        break;
                    cd = *c;
                }
                continue;
            }
        }

        size_t nh = (cd._chunk_head&INUSE_BITS)|(shm_alloc_chunk_getsize(&cd)+shm_alloc_chunk_getsize(&ncd));
        if(!shm_alloc_chunk_cas(c, &cd, nh, cd._chunk_expire_unit, cd._flags|MERGING_BIT))
        {
            shm_chunk_ptr ptr = allocator->_current_chunk_ptr; 
            if(shm_chunk_ptr_offset(&ptr) == (size_t)((char*)nc - (char*)allocator))
                try_step_current_chunk_ptr(allocator, now, &ptr, shm_chunk_ptr_offset(&ptr)+shm_alloc_chunk_getsize(&ncd));
            return true;
        }
        else
        {
            for(; ;){
                if(shm_alloc_chunk_cas(c, &cd, cd._chunk_head, cd._chunk_expire_unit, cd._flags &~ MERGING_BIT))
                    break;
                cd= *c;
            }
        }
    }
}

void *
alloc_large(shm_allocator_t * la, size_t n, size_t aligned_size, uint8_t add_chunk_flags)
{
    shm_allocator_t *allocator = la;
    n = aligne_space(n + HEAD_SIZE);
    if (!allow_aligned_size(aligned_size))
        return NULL;

    const uint16_t now = now_unit();
    const shm_chunk_ptr beg_ptr = la->_current_chunk_ptr;
    for(; ;){
        shm_chunk_ptr ptr = la->_current_chunk_ptr;
        if(shm_chunk_is_finish(&ptr,&beg_ptr))
            return NULL;

        shm_alloc_chunk_t * c = get_chunk(allocator,shm_chunk_ptr_offset(&ptr));
        shm_alloc_chunk_t cd = *c;
        if (ptr._data_ != la->_current_chunk_ptr._data_)
            continue;

        size_t cs = shm_alloc_chunk_getsize(&cd);
        shm_alloc_chunk_t * nc = (shm_alloc_chunk_t *) ((char *) c + cs);
        shm_alloc_chunk_t ncd = *nc;
        if (ptr._data_ != la->_current_chunk_ptr._data_)
            continue;

        size_t ncs = shm_alloc_chunk_getsize(&ncd);

        if(cd._flags & NEVER_FREE_BIT){
            if(ncd._flags & NEVER_FREE_BIT){
                if(shm_alloc_chunk_cas(c, &cd, (cs+ncs)|(cd._chunk_head&INUSE_BITS), 0, cd._flags)){
                    // 两个chunk合并后，后者的chunk的结构体信息大小空间回收，是sizeof(shm_alloc_chunk)，而不是后者永久块的内容 
                    if (ptr._data_ != la->_current_chunk_ptr._data_)
                    {
                        add_to_bins_and_fetch_total(allocator->_small_alloc_impl,nc,sizeof(shm_alloc_chunk_t));
                    }
                    else{
                        // 避免两个相邻的chunk同时被删的情况可能有问题
                        // 其他线程移动了_current_chunk_ptr，可能存在相邻两个chunk同时被删的情况，为避免并发冲突，丢弃shm_alloc_chunk结构体这8字节
                        // 极端情况:[NEVER_FREE_BIT块1][NEVER_FREE_BIT块2][NEVER_FREE_BIT块3],永久块1和2合并，同时另外的线程在合并永久块2和永久块3
                        // 否则永久块2的长度可能会变长
                    }
                }        
                continue;
            }   

            if(ncd._chunk_head & CINUSE_BIT && ncs <= 32){
                shm_alloc_chunk_t * nnc = (shm_alloc_chunk_t *) ((char *) nc + ncs);
                if (nnc->_flags & NEVER_FREE_BIT) {
                    // 若两个永久块之间仅有一小块空闲内存(32字节)，则尝试合并三个块
                    if(shm_alloc_chunk_cas(nc, &ncd, ncd._chunk_head|CINUSE_BIT, 0, 0)){
                        for(; ;){
                            if(shm_alloc_chunk_cas(c, &cd, (cs+ncs+shm_alloc_chunk_getsize(nnc)) | (cd._chunk_head & INUSE_BITS), 0, cd._flags)){
                                add_to_bins_and_fetch_total(allocator->_small_alloc_impl,nc,ncs+sizeof(shm_alloc_chunk_t));
                                break;
                            }
                            cd = *c;
                        }
                    }
                    continue;
                }
            }   
        }
        
        if(cd._chunk_head & (CINUSE_BIT|WAIT_MERGE)){
            // 若本块已被分配或处于等待合并状态，则跳转到下一块
            try_step_current_chunk_ptr(la, now, &ptr, shm_chunk_ptr_offset(&ptr)+cs);
            continue;
        }

        size_t afc = aligned_fill_count(shm_chunk_ptr_offset(&ptr)+SPACE_OFFSET,aligned_size);
        n += afc;

        if(cs < n){
            if(!try_merge_after(allocator, c, now)){
                size_t offset = shm_chunk_ptr_offset(&ptr) + cs;
                // 该判断是避免直接跳过下一永久块，以便于检测可能的合并
                if(!(ncd._flags & NEVER_FREE_BIT))
                    offset += ncs;
                try_step_current_chunk_ptr(la, now, &ptr, offset);
            }
        }
        else if (cs == n){
            // 直接返回该chunk
            if(shm_alloc_chunk_cas(c, &cd, cd._chunk_head|CINUSE_BIT, 0, add_chunk_flags))
            {
                for(; ;){
                    ncd = *nc;
                    // 由于本块已经被分配完毕，因此即使下一个块是等待合并状态，也不会被合并，应一并改为普通空闲状态
                    size_t nh = (ncd._chunk_head | PINUSE_BIT) & ~WAIT_MERGE;
                    if(shm_alloc_chunk_cas(c, &ncd, nh, ncd._chunk_expire_unit, ncd._flags))
                        break;
                }
                try_step_current_chunk_ptr(la, now, &ptr, shm_chunk_ptr_offset(&ptr)+cs);
                return split_for_aligned(c,afc);
            }
        }
        else {
            //切割chunk
            nc = (shm_alloc_chunk_t*)((char*)c +n);
            ncd = *nc;
            // 以下判断可保证ncd的取值是在c块分配之前（如果有的话）的值
            if( ptr._data_ == la->_current_chunk_ptr._data_ && cd._chunk_head == c->_chunk_head ){
                if(shm_alloc_chunk_cas(nc, &ncd, (cs-n)|PINUSE_BIT, 0, 0)){
                    if(shm_alloc_chunk_cas(c, &cd, n|CINUSE_BIT|(cd._chunk_head & PINUSE_BIT), 0, add_chunk_flags)){
                        try_step_current_chunk_ptr(la, now, &ptr, shm_chunk_ptr_offset(&ptr)+n);
                        return split_for_aligned(c,afc);
                    } 
                }
            }
        }
    }
}

bool
initialize_shm_allocator(shm_allocator_t *alloc, size_t length, size_t max_allocsize, bool safe_init){
    assert(alloc);
    assert(length >= sizeof(shm_allocator_t));

    length = length/16*16;
    if(safe_init){
        size_t old = alloc->_buffer_length;
        if(old == length || !ck_pr_cas_64(&alloc->_buffer_length,old,length))
            return false;
    }
    else{
        alloc->_buffer_length = length;
    }
    alloc->_version = 1000;
    alloc->_shm_type = SHM_TYPE_UNKNOWN;
    memset(&alloc->_reserve,0,sizeof(alloc->_reserve));
    alloc->_custom_data_ptr = NULL;
    alloc->_head_size = aligne_space16(sizeof(shm_allocator_t) + SPACE_OFFSET) - SPACE_OFFSET;

    shm_alloc_chunk_t * last_empty_chunk = get_last_chunk(alloc);
    last_empty_chunk->_chunk_head = CINUSE_BIT;

    shm_chunk_ptr_set(&alloc->_current_chunk_ptr, alloc->_head_size, 0, 0);

    shm_alloc_chunk_t * c = get_first_chunk(alloc);
    c->_chunk_head = PINUSE_BIT;

    alloc->_max_chunk_size = MAX_SIZE;
    alloc->_max_alloc_size = alloc->_max_chunk_size/2;

    c = get_chunk(alloc,shm_chunk_ptr_offset(&alloc->_current_chunk_ptr));
    size_t left_bytes = (char*)get_last_chunk(alloc) -(char*)c;
    const size_t max_size = alloc->_max_chunk_size - alloc->_max_alloc_size;
    bool is_beg = true;
    while(left_bytes > max_size){
        if(is_beg){
            c->_chunk_head = max_size | (c->_chunk_head & PINUSE_BIT);
            is_beg = false;
        }
        else{
            c->_chunk_head = max_size;
        }
        c = (shm_alloc_chunk_t*)((char*)c + max_size);
        left_bytes -= max_size;
    }

    if(left_bytes > 0){
        c->_chunk_head = left_bytes | (is_beg?(c->_chunk_head&PINUSE_BIT):0);
    }
    return initialize_shm_small_alloc_impl(alloc,max_allocsize);
}

static void
dump_chunk(shm_alloc_chunk_t * c)
{
    const char* cinuse = "----------";
    const char* pinuse = "----------";
    const char* waitmerge = "----------";
    const char* flags = "--------------";

    if(c->_chunk_head & CINUSE_BIT)
        cinuse = "CINUSE_BIT";
    if(c->_chunk_head & PINUSE_BIT)
        pinuse = "PINUSE_BIT";
    if(c->_chunk_head & WAIT_MERGE)
        waitmerge = "WAIT_MERGE";
    if(c->_flags & FLAGS_DELAY_BIT)
        flags = "DELAY_BIT";
    else if(c->_flags & NEVER_FREE_BIT)
        flags = "NEVER_FREE_BIT";

    fprintf(stdout,"[%p:%10lu, %s, %s, %s, %s]\r\n",(void*)c,shm_alloc_chunk_getsize(c),cinuse,pinuse,waitmerge,flags);
}

void
dump_shm_allocator(shm_allocator_t * a)
{
   static const char * SS[] = {"unknown", "posix", "sysv", "mmap"};
   const char * s = a->_shm_type >= sizeof(SS) / sizeof(SS[0]) ? "-" : SS[a->_shm_type];
   fprintf(stdout,"head: {buf: %p, buf_len: %lu, version: %u, shm-type: %s, head_size: %lu,\r\n \
                         _current_chunk_ptr: {%p,%u}, _custom_data_ptr: %p, now: %u}\r\n",
          (void*)a,a->_buffer_length,a->_version,s,a->_head_size,
          (void*)get_chunk(a,shm_chunk_ptr_offset(&a->_current_chunk_ptr)),
           a->_current_chunk_ptr._ver_,a->_custom_data_ptr,now_unit());

   shm_alloc_chunk_t * c = get_first_chunk(a); 
   shm_alloc_chunk_t * last = get_last_chunk(a); 

    while(c != last){
        dump_chunk(c);
        c = shm_alloc_chunk_nextchunk(c);
    }
}


static void
shm_assert_impl(bool succ, const char *__assertion, const char * __file, unsigned int __line, const char * __function)
{
    if(!succ){
#ifdef __assert_fail
    __assert_fail(__assertion, __file, __line, __function);
#else
    fprintf(stderr,"%s:%u, %s: failed assertion `%s'\n", __file, __line, __function, __assertion);
    abort();
#endif
    }
}

#define shm_assert(expr) shm_assert_impl(expr, #expr, __FILE__, __LINE__, __PRETTY_FUNCTION__)

CK_CC_INLINE static void 
assert_size_and_length(size_t ptr, shm_allocator_t * a)
{
    shm_assert(ptr >= a->_head_size && ptr <= a->_buffer_length);    
}

CK_CC_INLINE static void 
assert_large_chunk(void *ptr, char * base, size_t length)
{
    if(ptr != NULL){
        size_t offset = (char*)ptr - base;
        shm_assert(offset < length);
    }
}

CK_CC_INLINE static void 
assert_small_chunk(void *ptr, size_t length, ck_hs_t *set)
{
    if(ptr != NULL){
        char * p = (char *) ptr;
        //TODO:查找第一个比p大的地址
        (void)set;
        struct ck_hs_iterator it;
        shm_alloc_chunk_t * c = NULL;
        (void)it;
        shm_assert(p >= (char *) shm_alloc_chunk_getspace(c) && p + length - (char *) c <= (long) shm_alloc_chunk_getsize(c));
    }
}

typedef struct small_alloc_address{
    char *  _address;
    int     _size;
    struct small_alloc_address * next;
}small_alloc_address_t;

static unsigned long
hs_hash(const void *object, unsigned long seed)
{
    const char *c = object;
    unsigned long h;
    (void)seed;
    h = *(const unsigned long*)c;
    return h;
}

static bool
hs_compare(const void *previous, const void *compare)
{
    return *(const uint64_t*)previous > *(const uint64_t*)compare;
}

static void *
hs_malloc(size_t r)
{
    return malloc(r);
}

static void
hs_free(void *p, size_t b, bool r)
{
    (void)b;
    (void)r;
    free(p);
    return;
}

static struct ck_malloc my_allocator = { 
    hs_malloc,
    NULL,
    hs_free
};

static void
ht_hash_wrapper(ck_ht_hash_t *h, const void *key, size_t length, uint64_t seed)
{   
    h->value = (unsigned long)MurmurHash64A(key, length, seed);
    return;
} 

static void
shm_small_chunk_check_insert_address(small_alloc_address_t ** bins, char * entry, size_t len)
{
    small_alloc_address_t * temp = malloc(sizeof(small_alloc_address_t));
    temp->_address = entry;
    temp->_size = len;
    temp->next = NULL;

    if( NULL == *bins ){
        *bins = temp;
    }
    else{
        small_alloc_address_t * head = *bins;
        small_alloc_address_t * compare = *bins;
        while(compare && *(uint64_t*)(temp->_address) > *(uint64_t*)(compare->_address)){
            compare = compare->next; 
        }
        temp->next = compare->next;
        compare->next = temp;
        *bins = head;
    }
}

void
check_shm_allocator(char * base, size_t length)
{
    shm_allocator_t * a = (shm_allocator_t*)base;

    shm_assert(a->_buffer_length <= length);
    shm_assert(a->_head_size < a->_buffer_length);
    //shm_assert(a->_max_chunk_size < a->_buffer_length);
    shm_assert(a->_max_alloc_size <= a->_max_chunk_size);
    shm_assert(shm_chunk_ptr_offset(&a->_current_chunk_ptr) <= a->_buffer_length);
    shm_assert(shm_chunk_ptr_offset(&a->_current_chunk_ptr) >= a->_head_size);

    shm_alloc_chunk_t *c = get_first_chunk(a);
    shm_alloc_chunk_t * e = get_last_chunk(a); 
    shm_assert(e->_chunk_head & CINUSE_BIT);

    ck_ht_t ptr2head;
    ck_hs_t never_free_chunks;
    if (false == ck_ht_init(&ptr2head, CK_HT_MODE_BYTESTRING, ht_hash_wrapper, &my_allocator, 2, 6602834)){
        fprintf(stderr,"ck_ht_init error\r\n");
        abort();
    }

    if (false == ck_hs_init(&never_free_chunks, CK_HS_MODE_SPMC | CK_HS_MODE_OBJECT | CK_HS_MODE_DELETE, hs_hash, hs_compare, &my_allocator, 2, 6602834)){
        fprintf(stderr,"ck_hs_init error\r\n");
        abort();
    }

    ck_ht_hash_t hash_value;
    ck_ht_entry_t entry;

    for(; c < e; ){
        shm_alloc_chunk_t *nc = shm_alloc_chunk_nextchunk(c);
        size_t ptr = (char*)c - base;
        size_t h = c->_chunk_head;
        shm_assert((h & ~INUSE_BITS) > 0);

        assert_size_and_length(ptr, a);
        ck_ht_hash(&hash_value, &ptr2head, &ptr, sizeof(ptr));
        ck_ht_entry_set(&entry, hash_value, &ptr, sizeof(ptr), &h); 
        ck_ht_put_spmc(&ptr2head, hash_value, &entry);

        if(h & CINUSE_BIT){
            shm_assert(!(h & WAIT_MERGE));
        }
        else if(h & WAIT_MERGE){
            //shm_assert(!(h & PINUSE_BIT));
        }

        if(c->_flags & NEVER_FREE_BIT)
            ck_hs_put_unique(&never_free_chunks, hs_hash(c, never_free_chunks.seed), c);

        c = nc;
    }

    shm_assert(ck_hs_count(&never_free_chunks) > 0);
    shm_assert(c == e);

    size_t x = shm_chunk_ptr_offset(&a->_current_chunk_ptr);
    //TODO:查找包含x的个数
    //shm_assert(ck_ht_count(&ptr2head) != 0);
    (void)x;
    
    assert_large_chunk(&a->_shm_manager, base, length);
    shm_small_alloc_impl_t * small = a->_small_alloc_impl;
    assert_large_chunk(small, base, length);

    small_alloc_address_t * bins = NULL;
    for(int i = 0; i < (int)(sizeof(small->_small_bin)/sizeof(small->_small_bin[0])); ++i)
    {
        int len = i * 8 + 8;
        ck_shm_stack_entry_t *pentry;
        CK_SHM_STACK_FOREACH(&small->_small_bin[i], pentry){
            assert_small_chunk(pentry, len, &never_free_chunks);
            shm_small_chunk_check_insert_address(&bins,(char*)pentry,len);
        }
    }

    qnode_helper h;
    helper_setdata(&h,&small->_delay_q._push_tail);
    for (uint32_t i = h.n1; i != h.n2; ++i) {
        qnode_ptr p = small->_delay_q._v[i % small->_delay_q._max_size];
        char * address = qnode_ptr_get_ptr(&p, base);
        size_t len = qnode_ptr_get_size(&p);
        assert_small_chunk(address, len, &never_free_chunks);
        shm_small_chunk_check_insert_address(&bins,address,len);
    }

    small_alloc_address_t * p = bins;
    while(p && p->next)
    {
       small_alloc_address_t * n = p->next;
       shm_assert(p->_address + p->_size <= n->_address);
       p = p ->next;
    }
}

void *
get_container_parm1(struct shm_allocator * allocator, const char * name, bool create_if_not_exist, create_op_parm1 create_op)
{
    shm_manager_t * slist = shm_manager_t_ptr_get(&allocator->_shm_manager);
    struct shm_manager_info * n = NULL;
    const size_t name_len = strlen(name);
    const size_t len = sizeof(struct shm_manager_info) + name_len;
    n = alloc_ex(allocator, len);
    if (n == NULL){
        fprintf(stderr, "shm_manager alloc_ex failed, field: %s, len: %zu\r\n", name, len);
        return NULL; 
    }
    strcpy(n->_name, name);

    for (; ;) {
        struct shm_slist_pair pair = ck_shm_slist_search_if(&slist->slh_first, shm_manager_op, name, false);
        if (!create_if_not_exist){
            fprintf(stderr, "shm_manager field %s not exists but create_if_not_exist is false\r\n", name);
            if (n){
                free_ex(allocator, n, len, false);
            }
            return NULL; 
        }

        if (ck_shm_slist_insert_middle(pair.first, pair.second, &n->_list_entry))
        {
            create_op(allocator, &n->_impl);
            return void_ptr_get(&n->_impl);
        }
    }
}

void *
get_container_parm2(struct shm_allocator * allocator, const char * name, bool create_if_not_exist, size_t initialize_size, create_op_parm2 create_op)
{
    shm_manager_t * slist = shm_manager_t_ptr_get(&allocator->_shm_manager);
    struct shm_manager_info * n = NULL;
    const size_t name_len = strlen(name);
    const size_t len = sizeof(struct shm_manager_info) + name_len;
    n = alloc_ex(allocator, len);
    if (n == NULL){
        fprintf(stderr, "shm_manager alloc_ex failed, field: %s, len: %zu\r\n", name, len);
        return NULL; 
    }
    strcpy(n->_name, name);

    for (; ;) {
        struct shm_slist_pair pair = ck_shm_slist_search_if(&slist->slh_first, shm_manager_op, name, false);
        if (!create_if_not_exist){
            fprintf(stderr, "shm_manager field %s not exists but create_if_not_exist is false\r\n", name);
            if (n){
                free_ex(allocator, n, len, false);
            }
            return NULL; 
        }

        if (ck_shm_slist_insert_middle(pair.first, pair.second, &n->_list_entry))
        {
            create_op(allocator, &n->_impl, initialize_size);
            return void_ptr_get(&n->_impl);
        }
    }
}

static void                                                                            
create_stack_container(struct shm_allocator * allocator, void_ptr * container)                                               
{                                                                                      
    ck_shm_stack_t * stack = alloc_static(allocator, sizeof(ck_shm_stack_t));
    ck_shm_stack_init(stack);                                                          
    void_ptr_set(container, stack, false, false);
}  

ck_shm_stack_t *
get_stack(struct shm_allocator * allocator, const char * name, bool create_if_not_exist)
{
   return get_container_parm1(allocator,name,create_if_not_exist,create_stack_container); 
}

static void                                                                            
create_custom_object(struct shm_allocator * allocator, void_ptr * object, size_t initialize_size)                                               
{                                                                                      
    void * pb = alloc_static(allocator, initialize_size); 
    void_ptr_set(object, pb, false, false);
}  

void *
get_custom_object(struct shm_allocator * allocator, const char * name, size_t initialize_size, bool create_if_not_exist)
{
   return get_container_parm2(allocator,name,create_if_not_exist,initialize_size,create_custom_object); 
}

