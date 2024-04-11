
#include "sds.h"
#include "stdio.h"
#include <string.h>
int main(){

    // 1.sdsnewlen
    char *t  =  "sds";
    size_t len = strlen(t);
    printf("sdsnewlen-> %s\n", sdsnewlen(t, len));
    // 2.sdsnew
    printf("sdsnew-> %s\n", sdsnew(t));

    // 3.sdslen 15 中文
    printf("sdslen-> %zu\n",sdslen(sdsnew(t)));

    // 3.sdsavail 0 sdsnew调用的是sdsnewlen所以空间大小是按照len创建的 所以free为0
    printf("sdsavail-> %zu\n",sdsavail(sdsnew(t)));

    // 4.sdscatprintf
    printf("sdscatprintf-> %s\n", sdscatprintf(sdsnew(t), "%s %d %s", "asdas", 2, "ssss"));
    // 5.sdstrim
    printf("sdstrim-> %s\n",sdstrim(sdsnew("    aaaaabb    "), " "));

    printf("sdstrim-> %s\n",sdstrim(sdsnew("aaaxxxxaaaa"), "a"));
    // 6.sdsrange
    printf("sdsrange-> %s\n",sdsrange(sdsnew("aaaxxxxaaaa"), 0,-3));

    // 7.sdstolower
    sds b = sdsnew("AAAAAA");
    sdstolower(b);
    printf("sdstolower-> %s\n",b);
    // 8.sdssplitlen
    sds line = sdsnew("1 2");
    int argc = 4;
    printf("res:%s\n", line);
    sds* argv = sdssplitlen(line,sdslen(line)," ",1,&argc);
    printf("%s\n", argv[0]);
};