/*************************************************************************
	> File Name: validate.c
	> Author: 
	> Mail: 
	> Created Time: Wed 20 Dec 2017 04:15:33 PM CST
 ************************************************************************/

#include <stdio.h>
#include <malloc.h>
#include <ck_shm_allocator.h>

static 
void test(void){
    const int len = 100 * 1024 * 1024;
    void * buf= malloc(len);
    shm_allocator_t * allocator = buf;
    initialize_shm_allocator(allocator,len,1*1024,true);
    printf("---------------------------------------------------------------------------------------------\r\n");
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
    void *p1 = alloc_ex(allocator,523); 
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
    void *p2 = alloc_ex(allocator,523); 
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
    free_ex(allocator,p1,523,false);
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
    free_ex(allocator,p2,523,false);
    dump_shm_allocator(allocator);
    printf("---------------------------------------------------------------------------------------------\r\n");
    free(allocator);
}

int main(void){
    test();
    return 0;
}
