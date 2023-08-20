#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void *_malloc(size_t size, const char *filename, int line){
    void *ptr = malloc(size);
    
    char buffer[128] = {0};
    sprintf(buffer, "./memory/%p.memory", ptr);

    FILE *fp = fopen(buffer, "w");
    fprintf(fp, "[+]addr: %p, filename: %s, line: %d\n", ptr, filename, line);

    fflush(fp);
    fclose(fp);

    return ptr;
}

void _free(void *ptr, const char *filename, int line){
    char buffer[128] = {0};
    sprintf(buffer, "./memory/%p.memory", ptr);

    if (unlink(buffer) < 0){
        printf("double free: %p\n", ptr);
        return;
    }

    return free(ptr);
}

#define malloc(size)    _malloc(size, __FILE__, __LINE__)
#define free(ptr)       _free(ptr, __FILE__, __LINE__)

int main() {

    void *p1 = malloc(5);
    void *p2 = malloc(18);
    void *p3 = malloc(15);

    free(p1);
    free(p3);

}
