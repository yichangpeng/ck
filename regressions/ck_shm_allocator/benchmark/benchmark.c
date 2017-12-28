/*************************************************************************
	> File Name: benchmark.c
	> Author: 
	> Mail: 
	> Created Time: Thu 28 Dec 2017 02:10:01 PM CST
 ************************************************************************/

#include <stdio.h>
#include <malloc.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <ck_fifo.h>
#include <ck_shm_allocator.h>
#include "../../common.h"

struct test{
    char* p;
    unsigned int n;
};

static ck_fifo_mpmc_t fifo CK_CC_CACHELINE; 

unsigned int g_alloc_count = 0;
unsigned int g_free_count = 0;
unsigned int g_count = 1000000;

const int all_size = 199;

static void *
small_alloc_thread(void *arg){
    shm_allocator_t * allocator = arg;
    int n;
    struct test *pn;
    ck_fifo_mpmc_entry_t *fifo_entry;
    char *p;

    struct timeval start,end;
    common_gettimeofday(&start,NULL);
    for(unsigned int i = 0; i < g_count; ++i)
    {
        n = common_rand() % max_alloc_size + 1;
        p = alloc_ex(allocator,n);
        pn = malloc(sizeof(struct test));
        pn->p = p;
        pn->n = n;
        
        fifo_entry = malloc(sizeof(ck_fifo_mpmc_entry_t));
        ck_fifo_mpmc_enqueue(&fifo, fifo_entry, pn);
        ck_pr_inc_uint(&g_alloc_count);
    }
    common_gettimeofday(&end,NULL);
    fprintf(stdout,"small_alloc_thread eplase time:%lu ms, alloc count:%u\r\n",(end.tv_sec-start.tv_sec)*1000+(end.tv_usec-start.tv_usec)/1000,g_alloc_count);
    return NULL;
}

static void *                                                     
small_free_thread(void *arg){                                     
    shm_allocator_t * allocator = arg;                            
    ck_fifo_mpmc_entry_t *garbage;

    struct timeval start,end;                                     
    common_gettimeofday(&start,NULL);                             

    unsigned int i = 0;
    struct test *pn = NULL;
    while(true){
         if (true == ck_fifo_mpmc_dequeue(&fifo, &pn, &garbage)){
             free_ex(allocator,pn->p,pn->n,false);
            //free(pn);
            ck_pr_inc_uint(&g_free_count);
        }
        else{
            usleep(0);
            if(i++ >= g_count/10)
                break;           
        }
    }

    common_gettimeofday(&end,NULL);                               
    fprintf(stdout,"small_free_thread eplase time:%lu ms, free count:%u\r\n",(end.tv_sec-start.tv_sec)*1000+(end.tv_usec-start.tv_usec)/1000,g_free_count);                                                             
    return NULL;                                                  
}   

static void 
test_alloc_free(void *(*__alloc_start_routine) (void *), void *(*__free_start_routine) (void *)){
    const int len = 100 * 1024 * 1024;
    void * buf= malloc(len);
    shm_allocator_t * allocator = buf;
    initialize_shm_allocator(allocator,len,1*1024,true);

    struct timeval start,end;                                     
    common_gettimeofday(&start,NULL);                             
    const int thread_num = 10;
    pthread_t *thread = malloc(sizeof(pthread_t)*thread_num); 
    for(int i = 0; i < thread_num; ++i){
        if(i % 3 == 1)
        {
            int r = pthread_create(&thread[i], NULL, __free_start_routine, allocator);
            assert(r == 0);
        }
        else{
            int r = pthread_create(&thread[i], NULL, __alloc_start_routine, allocator);
            assert(r == 0);
        }
    }

    for(int i = 0; i < thread_num; ++i)
        pthread_join(thread[i], NULL);

    //printf("---------------------------------------------------------------------------------------------\r\n");
    //dump_shm_allocator(allocator);
    //printf("---------------------------------------------------------------------------------------------\r\n");
    common_gettimeofday(&end,NULL);                               
    fprintf(stdout,"test eplase time:%lu ms, alloc count:%u free count:%u\r\n",(end.tv_sec-start.tv_sec)*1000+(end.tv_usec-start.tv_usec)/1000,g_alloc_count,g_free_count);
    free(allocator);
}

static void 
test_quque(void){
}

static void 
test_slist(void){
}

static void 
test_hashmap(void){
}

static void 
test_skiplist(void){
    
}

int main(void){
    common_srand(time(NULL));
    ck_fifo_mpmc_init(&fifo, malloc(sizeof(ck_fifo_mpmc_entry_t)));
    
    test_alloc_free(small_alloc_thread,small_free_thread);
    test_quque();
    test_slist();
    test_hashmap();
    test_skiplist();
    return 0;
}
