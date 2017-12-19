/*************************************************************************
	> File Name: ck_shm_allocator.c
	> Author: 
	> Mail: 
	> Created Time: Mon 18 Dec 2017 05:37:10 PM CST
 ************************************************************************/

#include <sched.h>
#include <ck_shm_allocator.h>

inline static size_t 
aligned_fill_count(size_t n, size_t aligned_size)
{
    n &= aligned_size - 1;
    return n == 0 ? 0 : aligned_size - n;
}

static void * 
split_for_aligned(shm_alloc_chunk_t * c, size_t aligned_fill_count)
{
    if (aligned_fill_count == 0)
        return shm_alloc_chunk_getspace(c); 
    shm_alloc_chunk_t * nc = (shm_alloc_chunk_t *) ((char *) c + aligned_fill_count);
    nc->_chunk_head = (shm_alloc_chunk_getsize(c) - aligned_fill_count) | CINUSE_BIT;
    for (; ;) {
        shm_alloc_chunk_t cd = *c;
        if (shm_alloc_chunk_cas(c, &cd, aligned_fill_count | (cd._chunk_head & PINUSE_BIT), 0, 0))
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
     cpu_count = sysconf(_SC_NPROCESSORS_CONF);
#endif
    const int max_qsize = cpu_count * 25600;
    size_t n = sizeof(shm_small_alloc_impl_t) + sizeof(qnode_ptr) * max_qsize;
    shm_small_alloc_impl_t * sa = alloc_large(&allocator->_large_alloc_impl, n, 8, NEVER_FREE_BIT);
    if(sa == NULL)
        return false;

    shm_small_alloc_impl_init(sa, allocator);
    allocator->_small_alloc_impl = sa;
    memset(sa->_small_bin_total,0,sizeof(sa->_small_bin_total));

    for(size_t i = 0; i < sizeof(sa->_small_bin)/sizeof(sa->_small_bin[0]); ++i)
    {
        ck_stack_init(sa->_small_bin+i);
    }
    delay_queue_init(&sa->_delay_q, max_qsize);

    if (max_allocsize > 0 && max_allocsize < allocator->_max_alloc_size)
         allocator->_max_alloc_size = max_allocsize;
    return true;
}

static bool 
try_step_current_chunk_ptr(shm_large_alloc_impl_t * la, uint16_t now, shm_chunk_ptr* old, size_t offset)
{
    shm_allocator_t *allocator = la->_allocator;
    const bool restart = offset >= (allocator->_buffer_length - SPACE_OFFSET);
    if(shm_chunk_ptr_cas(&la->_current_chunk_ptr, old, restart?allocator->_head_size:offset, restart)){
        shm_alloc_chunk_t * c = get_chunk(allocator,offset);
        shm_alloc_chunk_t cd = *c;
        if((cd._flags & FLAGS_DELAY_BIT)
           && delay_free_expire(now, cd._chunk_expire_unit)
          )
        {
            if(shm_alloc_chunk_cas(c, &cd, cd._chunk_head, 0, 0))
                free_large(allocator->_small_alloc_impl, c);
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
                        free_large(allocator->_small_alloc_impl, nc);
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
            shm_chunk_ptr ptr = allocator->_large_alloc_impl._current_chunk_ptr; 
            if(shm_chunk_ptr_offset(&ptr) == (size_t)((char*)nc - (char*)allocator))
                try_step_current_chunk_ptr(&allocator->_large_alloc_impl, now, &ptr, shm_chunk_ptr_offset(&ptr)+shm_alloc_chunk_getsize(&ncd));
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
alloc_large(shm_large_alloc_impl_t * la, size_t n, size_t aligned_size, uint8_t add_chunk_flags)
{
    shm_allocator_t *allocator = la->_allocator;
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
                    //两个chunk合并后，后者的chunk
                    if (ptr._data_ != la->_current_chunk_ptr._data_)
                    {
                        add_to_bins_and_fetch_total(allocator->_small_alloc_impl,nc,sizeof(shm_alloc_chunk_t));
                    }
                    else{
                        // 其他线程移动了_current_chunk_ptr，可能存在相邻两个chunk同时被删的情况，为避免并发冲突，丢弃这8字节
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
                    if(shm_alloc_chunk_cas(nc, &ncd,(cs-n)|PINUSE_BIT|(cd._chunk_head & PINUSE_BIT), 0, add_chunk_flags)){
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
    alloc->_head_size = aligne_space(sizeof(shm_allocator_t) + SPACE_OFFSET) - SPACE_OFFSET;

    shm_alloc_chunk_t * last_empty_chunk = get_last_chunk(alloc);
    last_empty_chunk->_chunk_head = CINUSE_BIT;

    shm_chunk_ptr_set(&alloc->_large_alloc_impl._current_chunk_ptr, alloc->_head_size, 0, 0);

    shm_alloc_chunk_t * c = get_first_chunk(alloc);
    c->_chunk_head = PINUSE_BIT;

    alloc->_max_chunk_size = MAX_SIZE;
    alloc->_max_alloc_size = alloc->_max_chunk_size/2;

    c = get_chunk(alloc,shm_chunk_ptr_offset(&alloc->_large_alloc_impl._current_chunk_ptr));
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

