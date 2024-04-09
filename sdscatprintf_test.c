
#include "sds.h"
#include "stdio.h"

int main(){
    sds cmd = sdsnew("aaaaa   ");
    char *t = "sss";
    cmd = sdscatprintf(cmd, "my name is %s", "boby");
    printf("%s", cmd);
};