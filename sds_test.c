
#include "stdio.h"
typedef char *sds;


struct sdshdr {
    long len;
    long free;
    char buf[]
};

struct sdshdr sh = {6,2,{'h','2','3','\0'}};

int main() {
    struct sdshdr *s = &sh;
    printf("%s\n", s->buf);
    printf("%d\n",sizeof(struct sdshdr));
    sds sd = s->buf;
    struct sdshdr *new_sh = (void *)(sd - (sizeof (struct sdshdr)));
    printf(new_sh->buf);

}