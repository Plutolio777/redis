//
// Created by Dell on 2024/3/31.
//
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
char *strsep(char **stringp, const char *delim)
{
    char *s;
    const char *spanp;
    int c, sc;
    char *tok;
    if ((s = *stringp)== NULL)
        return (NULL);
    for (tok = s;;) {
        c = *s++;
        spanp = delim;
        do {
            if ((sc =*spanp++) == c) {
                if (c == 0)
                    s = NULL;
                else
                    s[-1] = 0;
                *stringp = s;
                return (tok);
            }
        } while (sc != 0);
    }
    /* NOTREACHED */
}

static char *prompt(char *line, int size) {
    char *retval;
    do {
        printf(">> ");
        // 读取输入
        retval = fgets(line, size, stdin);
        // 如果输入仅是回车的话则重复循环
    } while (retval && *line == '\n');
    // 转换成字符串
    line[strlen(line) - 1] = '\0';
    return retval;
}

int main(int argc, char ** argv) {
    int size = 4096, max = size >> 1;
    char buffer[size];
    char *line = buffer;
    char **ap, *args[max];

    while (prompt(line, size)) {
        argc = 0;
        // strsep 使用来进行字符串分割它的原理是 将字符串中的指定字符替换成\0 并循环获取分割的值
        // strsep(&line, " \t")
        //     ['a','b',' ','c','d','\t' ,'d'] -----> ['a','b','\0','c','d','\0' ,'d']
        for (ap = args; (*ap = strsep(&line, " \t")) != NULL;) {
            // args[0] != '\0'
            if (**ap != '\0') {
                if (argc >= max) break;
                // 如果遇到退出则终止进程
                if (strcasecmp(*ap,"quit") == 0 || strcasecmp(*ap,"exit") == 0)
                    exit(0);
                ap++;
                argc++;
            }
        }


        line = buffer;
    }

    exit(0);
}
