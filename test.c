//
// Created by Dell on 2024/4/1.
//
#include "stdio.h"
int main(){
    char *reply = "123";

    int ret = fwrite(reply, 3, 1, stdout);
    printf("\n%d", ret);
}