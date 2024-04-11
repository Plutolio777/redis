/* SDSLib, A C dynamic strings library
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define SDS_ABORT_ON_OOM

#include "sds.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
// malloc/free用于开辟和分配内存
#include "zmalloc.h"

/***
 * sdsOomAbort
 * 当无法分配sds内存时会抛出OOM异常
 */
static void sdsOomAbort(void) {
    fprintf(stderr,"SDS: Out Of Memory (SDS_ABORT_ON_OOM defined)\n");
    abort();
}

/***
 * sdsnewlen
 * 创建一个指定长度的sds字符串 或者根据指定的字符串创建sds
 * @param init 源字符数组
 * @param initlen 源字符数组长度
 * @return 返回一个sds字符串
 */
sds sdsnewlen(const void *init, size_t initlen) {
    // 定义一个sdshdr struct指针
    struct sdshdr *sh;
    // 分配内存 大小为 sizeof(struct sdshdr)+initlen+1 initlen是字符串长度
    // +1是因为还需要多存一个结束符 \0
    sh = zmalloc(sizeof(struct sdshdr)+initlen+1);

#ifdef SDS_ABORT_ON_OOM
    if (sh == NULL) sdsOomAbort();
#else
    if (sh == NULL) return NULL;
#endif
    // 填入初始长度
    sh->len = initlen;
    // 因为是按照字符串长度分配的空间 一次free剩余空间直接给0即可
    sh->free = 0;

    if (initlen) {
        // 将init中的内容copy到buf中
        if (init) memcpy(sh->buf, init, initlen);
        // 执行失败的话则清除数组内容
        else memset(sh->buf,0,initlen);
    }
    // 填入终止符
    sh->buf[initlen] = '\0';
    // 将buf地址返回 时间上就是返回sds 因为sds是char*
    return (char*)sh->buf;
}

/***
 * sdsempty
 * 创建一个空sds {0,0,{}}
 * @return 返回一个空sds
 */
sds sdsempty(void) {
    // 调用sdsnewlen时只需要传入空串就可以获得一个空的sds
    return sdsnewlen("",0);
}

/***
 * sdsnew
 * 根据字符串创建sds， 实际上是在调用sdsnewlen之前计算了一下字符串长度
 * @param init 源字符数组
 * @return 返回sds
 */
sds sdsnew(const char *init) {
    // 计算字符数组长度 传入sdsnewlen方法中
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init, initlen);
}

/**
 * sdslen
 * 获取sds的长度 经典方法通过指针移位获得sdshdr指针获取长度
 * @param s sds字符串
 * @return 返回sds字符串长度
 */
size_t sdslen(const sds s) {
    //sdshdr 结构体长度固定为两个long 所以当一个sds减去 struct时会获得结构体指针
    //从而可以用复杂度o(1)获取长度 这也是redis使用自定义sds的好处之一
    //这段代码是经典->如果从一个sds获得sdshdr
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    return sh->len;
}

/**
 * sdsdup
 * 复制sds
 * @param s 源sds
 * @return 获得新的sds
 */
sds sdsdup(const sds s) {
    // 通过sdslen获取一下sds长度就行了 只要理解sds也是char*就没啥东西
    return sdsnewlen(s, sdslen(s));
}
/**
 * sdsfree
 * 释放sds内存空间没啥好说的
 * @param s 需要释放内存的sds
 */
void sdsfree(sds s) {
    if (s == NULL) return;
    zfree(s-sizeof(struct sdshdr));
}
/**
 * sdsavail
 * 获取sds的剩余容量
 * @param s sds对象
 * @return 返回剩余容量
 */
size_t sdsavail(sds s) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    return sh->free;
}
/**
 * sdsupdatelen
 * 更新sds的长度
 * @param s sds对象
 */
void sdsupdatelen(sds s) {
    // 获取sdshdr结构体指针
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    // 使用原生函数计算字符串长度
    int reallen = strlen(s);
    // 调整长度和容量
    sh->free += (sh->len-reallen);
    sh->len = reallen;
}

/**
 *
 * 此方法为修改sds时需要确认是否容量充足 如果容量不够则需要进行扩容
 * @param s 扩容前的sds
 * @param addlen 新增加的长度
 * @return 扩容后的sds
 */
static sds sdsMakeRoomFor(sds s, size_t addlen) {
    struct sdshdr *sh, *newsh;
    // 获取sds的剩余空间
    size_t free = sdsavail(s);
    size_t len, newlen;
    // 如果容量充足 返回sds
    if (free >= addlen) return s;
    // 获得sds 长度
    len = sdslen(s);

    sh = (void*) (s-(sizeof(struct sdshdr)));
    // 扩容 目标容量为当前长度加新增长度 x 2
    newlen = (len+addlen)*2;
    // 重新分配内存
    newsh = zrealloc(sh, sizeof(struct sdshdr)+newlen+1);
#ifdef SDS_ABORT_ON_OOM
    if (newsh == NULL) sdsOomAbort();
#else
    if (newsh == NULL) return NULL;
#endif
    // 重新计算剩余容量 此处只是扩容 因此len是没有改变的
    newsh->free = newlen - len;
    // 返回sds
    return newsh->buf;
}

/**
 * sdscatlen
 * sds的拼接方法 目标字符串用头指针和长度表示
 * @param s 源sds
 * @param t 目标字符串指针
 * @param len 目标字符串长度
 * @return
 */
sds sdscatlen(sds s, void *t, size_t len) {
    struct sdshdr *sh;
    size_t curlen = sdslen(s);
    // 是否需要扩容
    s = sdsMakeRoomFor(s,len);
    if (s == NULL) return NULL;
    sh = (void*) (s-(sizeof(struct sdshdr)));
    // 直接进行内存拷贝，因为已经做过容量检查所以可以直接操作内存
    memcpy(s+curlen, t, len);
    // 重新调整长度和剩余大小
    sh->len = curlen+len;
    sh->free = sh->free-len;
    s[curlen+len] = '\0';
    return s;
}

/**
 *
 * sds的拼接方法 目标字符串用字符数组表示
 * 直接调用上面方法
 * @param s 源sds
 * @param t 目标字符串指针
 * @return 拼接之后的sds
 */
sds sdscat(sds s, char *t) {
    return sdscatlen(s, t, strlen(t));
}
/**
 * sdscpylen
 * 用提供的字符串直接覆盖sds中的内容
 * @param s 目标sds
 * @param t 需要覆盖的字符串
 * @param len 需要覆盖字符串的长度
 * @return 返回一个新的sds
 */
sds sdscpylen(sds s, char *t, size_t len) {
    // 获取字符串总长度
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    size_t totlen = sh->free+sh->len;
    // 如果复制字符串的长度大于当前sds的长度 则需要扩容
    if (totlen < len) {
        s = sdsMakeRoomFor(s,len-sh->len);
        if (s == NULL) return NULL;
        sh = (void*) (s-(sizeof(struct sdshdr)));
        totlen = sh->free+sh->len;
    }
    // 直接复制内容
    memcpy(s, t, len);
    // 调整字符串长度已经填充结尾符
    s[len] = '\0';
    sh->len = len;
    sh->free = totlen-len;
    return s;
}

/**
 * sdscpy
 * sds的拼接方法 目标字符串用字符数组表示
 * 直接调用上面方法
 * @param s 目标sds
 * @param t 需要覆盖的字符串
 * @return 返回一个新的sds
 */
sds sdscpy(sds s, char *t) {
    return sdscpylen(s, t, strlen(t));
}

/**
 *
 * 这里出现了几个原生库中的结构和函数
 * @va_list 是在C语言中解决变参问题的一组宏，变参问题是指参数的个数不定，
 * 可以是传入一个参数也可以是多个;可变参数中的每个参数的类型可以不同,
 * 也可以相同;可变参数的每个参数并没有实际的名称与之相对应，用起来是很灵活。
 * @va_list表示可变参数列表类型，实际上就是一个char指针fmt。
 * 使用@vsnprintf()用于向一个字符串缓冲区打印格式化字符串，且可以限定打印的格式化字符串的最大长度。
 * 这个方式是用来进行格式化打印sds的
 * 具体使用方法可以参见sdscatprintf_test.c
 * cmd = sdscatprintf(cmd, "my name is %s", "boby") --> sds("my name is boby")
 * @param s 目标sds
 * @param fmt 字符串格式
 * @param ... 字符串参数
 * @return 拼接后的字符串
 */
sds sdscatprintf(sds s, const char *fmt, ...) {
    va_list ap;
    char *buf, *t;
    size_t buflen = 16;

    while(1) {
        buf = zmalloc(buflen);
#ifdef SDS_ABORT_ON_OOM
        if (buf == NULL) sdsOomAbort();
#else
        if (buf == NULL) return NULL;
#endif
        //在倒数第二个位置设置终止符
        buf[buflen-2] = '\0';
        // 使用 用@va_start宏初始化变量刚定义的va_list变量；
        va_start(ap, fmt);
        // 将ap中的可变参数 按照fmt中的模板进行格式化输出到buf中
        vsnprintf(buf, buflen, fmt, ap);
        va_end(ap);
        // 如果buf长度超了则会导致倒数第二位不是终止符
        // 所以要增大buflen重新赋值
        // 因为字符串格式化不太好计算生成后的长度 因此采用这种方式来处理
        if (buf[buflen-2] != '\0') {
            zfree(buf);
            buflen *= 2;
            continue;
        }
        break;
    }
    // 将buf中的内容拼接到sds中
    t = sdscat(s, buf);
    zfree(buf);
    return t;
}
/**
 *
 * 去除sds两边的 所有 cset指定的字符
 * @param s 目标sds
 * @param cset 指定需要去除的字符
 * @return 返回处理后的sds
 */
sds sdstrim(sds s, const char *cset) {
    // 获取sds头指针
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    char *start, *end, *sp, *ep;
    size_t len;
    // 指针指向sds头
    sp = start = s;

    // 指针指向sds尾
    ep = end = s+sdslen(s)-1;
    //       <- ep-sp ->
    //-->   sp          ep <---
    //       |          |
    //       V          V
    // "     abcdaaaaaaae   "
    // 从头遍历找到不等于cset的位置 sp
    while(sp <= end && strchr(cset, *sp)) sp++;
    // 从尾指针遍历找到不等于cset的位置 ep
    while(ep > start && strchr(cset, *ep)) ep--;
    // 如果sp > ep 说明字符串左边不需要清理,
    len = (sp > ep) ? 0 : ((ep-sp)+1);
    // 如果sp不为buf的头指针位置 则将sds从左边平移到指针头
    if (sh->buf != sp) memmove(sh->buf, sp, len);
    // 右边的缓存不需要清理 只需要结尾加入终止符即可
    sh->buf[len] = '\0';
    //重新计算长度和容量
    sh->free = sh->free+(sh->len-len);
    sh->len = len;
    return s;
}

/**
 *
 * 获取sds中从start到end的内容获取一个新的sds
 * @param s 目标sds
 * @param start 起始位置
 * @param end 结束为止
 * @return 返回修改后的sds
 */
sds sdsrange(sds s, long start, long end) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    size_t newlen, len = sdslen(s);

    if (len == 0) return s;
    // 如果start小于0则从尾部获取 如 -1 则start=len-1为sds最后一位
    if (start < 0) {
        start = len+start;
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end = len+end;
        if (end < 0) end = 0;
    }
    // 计算sds新长度
    newlen = (start > end) ? 0 : (end-start)+1;
    if (newlen != 0) {
        if (start >= (signed)len) start = len-1;
        if (end >= (signed)len) end = len-1;
        newlen = (start > end) ? 0 : (end-start)+1;
    } else {
        start = 0;
    }
    // 通过平移来获得新的字符串
    if (start != 0) memmove(sh->buf, sh->buf+start, newlen);
    // 加入终止符和重新计算长度
    sh->buf[newlen] = 0;
    sh->free = sh->free+(sh->len-newlen);
    sh->len = newlen;
    return s;
}

/*
 * sdstolower
 * 将sds全变为小写 遍历字符数组调用tolower即可
 */
void sdstolower(sds s) {
    int len = sdslen(s), j;

    for (j = 0; j < len; j++) s[j] = tolower(s[j]);
}
/*
 * sdstolower
 * 将sds全变为大写 遍历字符数组调用tolower即可
 */
void sdstoupper(sds s) {
    int len = sdslen(s), j;

    for (j = 0; j < len; j++) s[j] = toupper(s[j]);
}
/**
 *
 * 比较字符串大小
 * @param s1
 * @param s2
 * @return
 */
int sdscmp(sds s1, sds s2) {
    size_t l1, l2, minlen;
    int cmp;
    // 计算两个sds长度
    l1 = sdslen(s1);
    l2 = sdslen(s2);
    // 计算两个最小长度
    minlen = (l1 < l2) ? l1 : l2;
    // 调用系统函数比较s1 s2 两个字符串交集大小
    cmp = memcmp(s1,s2,minlen);
    // 如果前minlen都相等 则返回长度更大的那个
    if (cmp == 0) return l1-l2;
    return cmp;
}

/* Split 's' with separator in 'sep'. An array
 * of sds strings is returned. *count will be set
 * by reference to the number of tokens returned.
 *
 * On out of memory, zero length string, zero length
 * separator, NULL is returned.
 *
 * Note that 'sep' is able to split a string using
 * a multi-character separator. For example
 * sdssplit("foo_-_bar","_-_"); will return two
 * elements "foo" and "bar".
 *
 * This version of the function is binary-safe but
 * requires length arguments. sdssplit() is just the
 * same function but for zero-terminated strings.
 */
/**
 * sdssplitlen
 * 将sds按照指定的分割符分隔成sds数组
 * @param s 目标sds
 * @param len sds的长度
 * @param sep 分隔符
 * @param seplen 分割符的长度
 * @param count count是用来表明返回的数组个数的
 * @return
 */
sds *sdssplitlen(char *s, int len, char *sep, int seplen, int *count) {
    int elements = 0, slots = 5, start = 0, j;
    // 开辟5个sds内存用于存放sds数组
    sds *tokens = zmalloc(sizeof(sds)*slots);
#ifdef SDS_ABORT_ON_OOM
    if (tokens == NULL) sdsOomAbort();
#endif
    if (seplen < 1 || len < 0 || tokens == NULL) return NULL;
    if (len == 0) {
        *count = 0;
        return tokens;
    }
    // 遍历sds字符
    for (j = 0; j < (len-(seplen-1)); j++) {
        /* make sure there is room for the next element and the final one */
        // 如果数组容量小于存放数+2 则说明库存块不足了 需要提前扩容
        if (slots < elements+2) {
            sds *newtokens;
            // 扩容策略就是当前槽位*2
            slots *= 2;
            // 重新分配内存
            newtokens = zrealloc(tokens,sizeof(sds)*slots);
            if (newtokens == NULL) {
#ifdef SDS_ABORT_ON_OOM
                sdsOomAbort();
#else
                goto cleanup;
#endif
            }
            tokens = newtokens;
        }
        /* search the separator */
        // 这里的s为sds字符数组的守地址
        // 1. (seplen == 1 && *(s+j) == sep[0]) 当分割符为1是直接比较对应位置上的字符是否等于分割符
        // 2. 如果分割符大于1 则需要使用memcap来对比一段字符谁大谁小了 如果为0则相等
        if ((seplen == 1 && *(s+j) == sep[0]) || (memcmp(s+j,sep,seplen) == 0)) {
            // 说明匹配到了对数组相应位置进行赋值
            tokens[elements] = sdsnewlen(s+start,j-start);
            if (tokens[elements] == NULL) {
#ifdef SDS_ABORT_ON_OOM
                sdsOomAbort();
#else
                goto cleanup;
#endif
            }
            elements++;
            start = j+seplen;
            j = j+seplen-1; /* skip the separator */
        }
    }
    /* Add the final element. We are sure there is room in the tokens array. */
    // 添加最后一个元素 上面的循环只有在匹配到分割符的时候将分割符前面的内容装入数组
    // 1 2 如果存在着中情况且2后面不存在分割符就需要处理最后一个元素
    tokens[elements] = sdsnewlen(s+start,len-start);
    if (tokens[elements] == NULL) {
#ifdef SDS_ABORT_ON_OOM
                sdsOomAbort();
#else
                goto cleanup;
#endif
    }
    elements++;
    *count = elements;
    return tokens;

#ifndef SDS_ABORT_ON_OOM
cleanup:
    {
        int i;
        for (i = 0; i < elements; i++) sdsfree(tokens[i]);
        zfree(tokens);
        return NULL;
    }
#endif
}
