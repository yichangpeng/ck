/*************************************************************************
	> File Name: validate.c
	> Author: 
	> Mail: 
	> Created Time: Wed 20 Dec 2017 04:15:33 PM CST
 ************************************************************************/

#include <stdio.h>
#include <malloc.h>
#include <ck_shm_allocator.h>

static void 
test_alloc_free(void){
    const int len = 100 * 1024 * 1024;
    void * buf= malloc(len);
    shm_allocator_t * allocator = buf;
    initialize_shm_allocator(allocator,len,1*1024,true);
    printf("---------------------------------------------------------------------------------------------\r\n");
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
    void *p1 = alloc_ex(allocator,23); 
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
    void *p2 = alloc_ex(allocator,23); 
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
    void *p3 = alloc_ex(allocator,23); 
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
    free_ex(allocator,p1,23,false);
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
    free_ex(allocator,p2,23,false);
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
    free_ex(allocator,p3,23,false);
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
    free(allocator);
}

struct Test{
    int score;
    char name[12];
    CK_STAILQ_ENTRY(Test) list_entry;
};

DEF_QUEUE_IMPL(Test_queue, Test)

static void 
test_queue(void){
    const int len = 100 * 1024 * 1024;
    void * buf= malloc(len);
    shm_allocator_t * allocator = buf;
    initialize_shm_allocator(allocator,len,1*1024,true);
    printf("---------------------------------------------------------------------------------------------\r\n");
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
 
    struct Test_queue* list = GET_QUEUE(allocator,"test_queue",true,Test);
    for(int i = 0; i < 10; ++i)
    {
        struct Test *temp = malloc(sizeof(struct Test));
        temp->score = i;
        snprintf(temp->name,10,"score%d:",i);
        CK_STAILQ_INSERT_HEAD(list, temp, list_entry);
    }
    struct Test *n = NULL;
    CK_STAILQ_FOREACH(n, list, list_entry) {
       printf("%s %d\r\n",n->name,n->score); 
    }
    
    printf("---------------------------------------------------------------------------------------------\r\n");
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
}

int main(void){
    test_alloc_free();
    test_queue();
    return 0;
}
