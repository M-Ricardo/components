// gcc -o fun2 fun2.c -ldl -g

#define _GNU_SOURCE
#include <dlfcn.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <link.h>


typedef void *(*malloc_t)(size_t size);
malloc_t malloc_f = NULL;

typedef void (*free_t)(void *ptr);
free_t free_f = NULL;


int enable_malloc_hook = 1;
int enable_free_hook = 1;

void *malloc(size_t size){


    void *ptr = NULL;
    if (enable_malloc_hook ){
        enable_malloc_hook = 0; 
        enable_free_hook = 0;

        ptr = malloc_f(size);

        void *caller = __builtin_return_address(0);

        char buffer[128] = {0};
        sprintf(buffer, "./memory/%p.memory", ptr);

        FILE *fp = fopen(buffer, "w");
        fprintf(fp, "[+] caller: %p, addr: %p, size: %ld\n", caller, ptr, size);

        fflush(fp);
        fclose(fp);

        enable_malloc_hook = 1;
        enable_free_hook = 1;
    }
    else {
        ptr = malloc_f(size);
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

        free_f(ptr);

        enable_malloc_hook = 1;
        enable_free_hook = 1;
    }
    else {

        free_f(ptr);
    }
}

void init_hook(){
    if (!malloc_f){
        malloc_f = dlsym(RTLD_NEXT, "malloc");
    }

    if (!free_f){
        free_f = dlsym(RTLD_NEXT, "free");
    }
}
int main(){
    init_hook();

    void *p1 = malloc(5);
    void *p2 = malloc(18);
    void *p3 = malloc(15);

    free(p1);
    free(p3);

}
