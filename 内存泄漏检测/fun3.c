// gcc -o fun3 fun3.c
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <link.h>


void* converToELF(void *addr) {
    Dl_info info;
    struct link_map *link;
    dladdr1(addr, &info, (void **)&link, RTLD_DL_LINKMAP);
    // printf("%p\n", (void *)(size_t)addr - link->l_addr);
    
    return (void *)((size_t)addr - link->l_addr);
}



extern void *__libc_malloc(size_t size);
extern void *__libc_free(void *ptr);


int enable_malloc_hook = 1;
int enable_free_hook = 1;

void *malloc(size_t size){


    void *ptr = NULL;
    if (enable_malloc_hook ){
        enable_malloc_hook = 0; 
        enable_free_hook = 0;

        ptr = __libc_malloc(size);

        void *caller = __builtin_return_address(0);

        char buffer[128] = {0};
        sprintf(buffer, "./memory/%p.memory", ptr);

        FILE *fp = fopen(buffer, "w");
        fprintf(fp, "[+] caller: %p, addr: %p, size: %ld\n", converToELF(caller), ptr, size);

        fflush(fp);
        fclose(fp);

        enable_malloc_hook = 1;
        enable_free_hook = 1;
    }
    else {
        ptr = __libc_malloc(size);
    }
    return ptr;
}

void free(void *ptr){

    if (enable_free_hook ){
        enable_free_hook = 0;
        enable_malloc_hook = 0;

        char buffer[128] = {0};
        sprintf(buffer, "./memory/%p.memory", ptr);

        if (unlink(buffer) < 0){
            printf("double free: %p\n", ptr);
            return;
        }

        __libc_free(ptr);

        enable_malloc_hook = 1;
        enable_free_hook = 1;
    }
    else {

        __libc_free(ptr);
    }
}


int main(){

    void *p1 = malloc(5);
    void *p2 = malloc(18);
    void *p3 = malloc(15);

    free(p1);
    free(p3);

}
