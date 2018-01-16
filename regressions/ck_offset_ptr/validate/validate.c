/*************************************************************************
	> File Name: test.c
	> Author: 
	> Mail: 
	> Created Time: Mon 08 Jan 2018 04:58:13 PM CST
 ************************************************************************/

#include <stdio.h>
#include <ck_stddef.h>
#include <ck_offset_ptr.h>

struct ck_test;
typedef struct ck_test ck_test_t;
ck_offset_ptr(int_config,
              int_ptr,
              change_from,
              get,
              set,
              cas,ck_test_t,
              0,
              47)

struct ck_test{
    int num; 
    int_ptr next;
};

int main(void){
    struct ck_test a = {100,CK_OFFSET_PTR_ENTRY_NULL};
    struct ck_test b = {200,CK_OFFSET_PTR_ENTRY_NULL};
    struct ck_test c = {300,CK_OFFSET_PTR_ENTRY_NULL};
    
    set(&a.next,&b,false,false);
    set(&b.next,&c,false,false);
    void *pa = get(&a.next);
    printf("num=%d\r\n",((ck_test_t*)pa)->num);

    cas(&a.next,&a.next,&b.next);
    pa = get(&a.next);
    printf("num=%d\r\n",((ck_test_t*)pa)->num);

    
    cas_ptr(&a.next,&c,&b);
    pa = get(&a.next);
    printf("num=%d\r\n",((ck_test_t*)pa)->num);

    change_from(&c.next,&a.next);
    pa = get(&a.next);
    printf("pa=%p\r\n",pa);

    int_ptr offset_ptr_a = CK_OFFSET_PTR_ENTRY_NULL;
    pa = get(&offset_ptr_a);
    printf("a=%d,&a=%p,pa=%p,&b=%p\r\n",a.num,(void*)&a,pa,(void*)&b);

    int_ptr offset_ptr_b;
    set(&offset_ptr_b,NULL,false,false);
    pa = get(&offset_ptr_b);
    printf("111a=%d,&a=%p,pa=%p,&b=%p\r\n",a.num,(void*)&a,pa,(void*)&b);
    printf("%s\r\n",is_offset_ptr_null(int_config,&offset_ptr_a)?"true":"false");

    offset_ptr_a.offset_data = 1;
    pa = get(&offset_ptr_a);
    printf("a=%d,&a=%p,pa=%p,&b=%p\r\n",a.num,(void*)&a,pa,(void*)&b);

    printf("%s\r\n",is_offset_ptr_null(int_config,&offset_ptr_a)?"true":"false");

    return 0;
}
