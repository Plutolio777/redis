/* Hash Tables Implementation.
 *
 * This file implements in memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
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

#include "fmacros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <limits.h>

#include "dict.h"
#include "zmalloc.h"

/* ---------------------------- Utility funcitons --------------------------- */

/**
 * *fmt,...结合va_list 进行格式化错误输出
 * @param fmt
 * @param ...
 */
static void _dictPanic(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "\nDICT LIBRARY PANIC: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n\n");
    va_end(ap);
}

/* ------------------------- Heap Management Wrappers------------------------ */

/**
 * 分配内存时候的一个包装器 如果失败了则panic 调用zmalloc分配内存
 * @param size 需要分配的内存大小
 * @return
 */
static void *_dictAlloc(size_t size)
{
    void *p = zmalloc(size);
    if (p == NULL)
        _dictPanic("Out of memory");
    return p;
}

/**
 * 调用zfree释放内存
 * @param ptr
 */
static void _dictFree(void *ptr) {
    zfree(ptr);
}

/* -------------------------- private prototypes ---------------------------- */

static int _dictExpandIfNeeded(dict *ht);
static unsigned long _dictNextPower(unsigned long size);
static int _dictKeyIndex(dict *ht, const void *key);
static int _dictInit(dict *ht, dictType *type, void *privDataPtr);

/* -------------------------- hash functions -------------------------------- */

/* Thomas Wang's 32 bit Mix Function */
/**
 * 这是一个hash值打散的一个算法 作用是减少hash冲突 采用的是位运算的思想
 * @param key
 * @return
 */
unsigned int dictIntHashFunction(unsigned int key)
{
    key += ~(key << 15);
    key ^=  (key >> 10);
    key +=  (key << 3);
    key ^=  (key >> 6);
    key += ~(key << 11);
    key ^=  (key >> 16);
    return key;
}

/* Identity hash function for integer keys */
unsigned int dictIdentityHashFunction(unsigned int key)
{
    return key;
}

/* Generic hash function (a popular one from Bernstein).
 * I tested a few and this was the best. */
/**
 * redis选择的通用hash函数
 * @param buf 字符串
 * @param len 字符串的长度
 * @return
 */
unsigned int dictGenHashFunction(const unsigned char *buf, int len) {
    unsigned int hash = 5381;

    while (len--)
        hash = ((hash << 5) + hash) + (*buf++); /* hash * 33 + c */
    return hash;
}

/* ----------------------------- API implementation ------------------------- */

/* Reset an hashtable already initialized with ht_init().
 * NOTE: This function should only called by ht_destroy(). */
/**
 * 还原一个dict
 * 只能由 ht_destroy调用
 * @param ht
 */
static void _dictReset(dict *ht)
{
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

/* Create a new hash table */
/**
 * 创建一个新的dict
 * @param type  传入dictType
 * @param privDataPtr 私有值指针
 * @return 创建好的dict
 */
dict *dictCreate(dictType *type, void *privDataPtr)
{
    // 分配dict内存空间
    dict *ht = _dictAlloc(sizeof(*ht));
    // 然后调用init方法初始化一个dict
    _dictInit(ht,type,privDataPtr);
    return ht;
}

/* Initialize the hash table */
/**
 * dict的初始化
 * @param ht 目标dict
 * @param type dict类型
 * @param privDataPtr 私有值
 * @return 返回初始化的结果 此时的dict的容量还为0
 */
int _dictInit(dict *ht, dictType *type, void *privDataPtr)
{
    // 冲值dict的所有值
    _dictReset(ht);
    ht->type = type;
    ht->privdata = privDataPtr;
    return DICT_OK;
}

/* Resize the table to the minimal size that contains all the elements,
 * but with the invariant of a USER/BUCKETS ration near to <= 1 */
/**
 * map的扩容方法包装
 * @param ht 传入的map
 * @return
 */
int dictResize(dict *ht)
{
    // 获取map中使用的容量
    int minimal = ht->used;
    // 当前容量如果小于 初始容量4则调整为4
    if (minimal < DICT_HT_INITIAL_SIZE)
        minimal = DICT_HT_INITIAL_SIZE;
    // 调用该方法进行扩容
    return dictExpand(ht, minimal);
}

/**
 * 扩展或者创建map
 * @param ht map对象
 * @param size 需要扩容的最小容量
 * @return
 */
/* Expand or create the hashtable */
int dictExpand(dict *ht, unsigned long size)
{
    // 新的hashtable
    dict n; /* the new hashtable */

    // 调整size向上取证为2的幂次方
    unsigned long realsize = _dictNextPower(size), i;

    /* the size is invalid if it is smaller than the number of
     * elements already inside the hashtable */
    if (ht->used > size)
        return DICT_ERR;
    // 初始新map
    _dictInit(&n, ht->type, ht->privdata);
    n.size = realsize;
    n.sizemask = realsize-1;
    // 分配新map中table数组的内存
    n.table = _dictAlloc(realsize*sizeof(dictEntry*));

    /* Initialize all the pointers to NULL */
    // 调整map中所有的entry指针指向0也就是NULL
    memset(n.table, 0, realsize*sizeof(dictEntry*));

    /* Copy all the elements from the old to the new table:
     * note that if the old hash table is empty ht->size is zero,
     * so dictExpand just creates an hash table. */
    // 这里开始复制就表的内容
    n.used = ht->used;
    for (i = 0; i < ht->size && ht->used > 0; i++) {
        // 当前entry和下一个entry的指针
        dictEntry *he, *nextHe;
        // 第i位为NULL则重复
        if (ht->table[i] == NULL) continue;
        
        /* For each hash entry on this slot... */
        // entry链的头结点
        // [][][entry][][][]
        //       V
        //     entry
        //       V
        //     entry
        he = ht->table[i];
        // 开始遍历采用双指针遍历entry链 这里的扩容跟JAVA HashMap扩容机制类似
        while(he) {
            unsigned int h;

            nextHe = he->next;
            /* Get the new element index */
            // dictHash是计算key的hash值 有三种类型hashtable采用的是同一种hash算法
            // dictGenHashFunction 作者的注释说这种是最好的 但是也留了两种其他的算法可以学习一下
            // 注意这个n.sizemask 实际上位 length-1
            // 这里是经典的 hash & (n-1) 取模运算 能够提高去摸的效率
            // 这也是为啥在扩容的时候需要将length调整为2的幂了
            // 经过下面的操作就可以得到entry在新table中的索引了
            // 数组长度为2的幂次方还有一种好处就是 h这个位置要么在原位置要么等于原位置+原长度

            h = dictHashKey(ht, he->key) & n.sizemask;
            // 这两段代码表示扩容的方式是采用头插法 比如原map
            // [][][][1][][][]
            //       [2]
            //       [3]
            //                     he->next=n.table[h] n.table[h] = he
            // 第一次循环                  [1]
            //  [][][][N][][][]    [][][][N][][][] [][][][1][][][]
            //  next> [2]                                [N]
            //        [3]
            //                   he->next = n.table[h] n.table[h] = he
            // 第二次遍历                 [2]
            // [][][][N][][][]    [][][][1][][][]  [][][][2][][][]
            //                          [N]              [1]
            // next> [3]                                 [N]
            //                   he->next = n.table[h] n.table[h] = he
            // 第三次遍历                 [3]
            // [][][][N][][][]    [][][][2][][][]  [][][][3][][][]
            //                          [1]              [2]
            // next> NULL                                [1]
            he->next = n.table[h];
            n.table[h] = he;
            ht->used--;
            /* Pass to the next element */
            he = nextHe;
        }
    }
    assert(ht->used == 0);
    //释放旧hashtable的数组
    _dictFree(ht->table);

    /* Remap the new hashtable in the old */
    //调整指针
    *ht = n;
    return DICT_OK;
}

/**
 * 添加一个元素到hashmap
 * 这个方法看起来是尝试插入如果key已经存在则返回1
 * @param ht map
 * @param key
 * @param val
 * @return
 */
int dictAdd(dict *ht, void *key, void *val)
{
    int index;
    // 准备一个data entry对象存放数据
    dictEntry *entry;

    /* Get the index of the new element, or -1 if
     * the element already exists. */
    // 获取key在table中的索引
    if ((index = _dictKeyIndex(ht, key)) == -1)
        return DICT_ERR;

    /* Allocates the memory and stores key */
    // 分配entry的内存 内存不够则OOM
    entry = _dictAlloc(sizeof(*entry));
    // 头插法插入元素即可
    entry->next = ht->table[index];
    ht->table[index] = entry;

    /* Set the hash entry fields. */
    dictSetHashKey(ht, entry, key);
    dictSetHashVal(ht, entry, val);
    ht->used++;
    return DICT_OK;
}

/* Add an element, discarding the old if the key already exists.
 * Return 1 if the key was added from scratch, 0 if there was already an
 * element with such key and dictReplace() just performed a value update
 * operation. */
/**
 * 这个方法是dictAdd的包装 如果插入失败则修改
 * 这个方法感觉是可以优化的 因为这里在dictAdd dictFind会两次查找key 应该一次就可以搞定
 * @param ht
 * @param key
 * @param val
 * @return
 */
int dictReplace(dict *ht, void *key, void *val)
{
    dictEntry *entry, auxentry;

    /* Try to add the element. If the key
     * does not exists dictAdd will suceed. */
    // 首先尝试直接添加元素 如果成功了则直接返回
    if (dictAdd(ht, key, val) == DICT_OK)
        return 1;
    /* It already exists, get the entry */
    // 如果已经存在这个key的话
    entry = dictFind(ht, key);
    /* Free the old value and set the new one */
    /* Set the new value and free the old one. Note that it is important
     * to do that in this order, as the value may just be exactly the same
     * as the previous one. In this context, think to reference counting,
     * you want to increment (set), and then decrement (free), and not the
     * reverse. */
    // 保存一下指针 后面会释放内存
    auxentry = *entry;
    dictSetHashVal(ht, entry, val);
    dictFreeEntryVal(ht, &auxentry);
    return 0;
}

/* Search and remove an element */
/**
 * 从hashtable中删除元素
 * @param ht
 * @param key
 * @param nofree
 * @return
 */
static int dictGenericDelete(dict *ht, const void *key, int nofree)
{
    unsigned int h;
    // 准备两个entry
    dictEntry *he, *prevHe;
    // 如果size为0就不需要操作了 返回删除失败
    if (ht->size == 0)
        return DICT_ERR;
    // 计算索引值
    h = dictHashKey(ht, key) & ht->sizemask;
    // 根据索引获取entry链
    he = ht->table[h];
    // 开始遍历entry链表
    prevHe = NULL;
    while(he) {
        // 如果找到相同key
        if (dictCompareHashKeys(ht, key, he->key)) {
            /* Unlink the element from the list */
            if (prevHe)
                // 如果是中间节点则需要将前置节点的next指向自己的next
                prevHe->next = he->next;
            else
                // 如果是头节点这table的索引为止直接放自己的下一个entry即可
                ht->table[h] = he->next;
            // 如果需要释放key value的内存
            if (!nofree) {
                dictFreeEntryKey(ht, he);
                dictFreeEntryVal(ht, he);
            }
            // 释放entry内存
            _dictFree(he);
            // 容量减1
            ht->used--;
            return DICT_OK;
        }
        prevHe = he;
        he = he->next;
    }
    // 进行到这里说明对应索引为止没有entry 删除失败没有找到
    return DICT_ERR; /* not found */
}

/*
 * dictGenericDeletey一层调用包装 默认是会清除key和value内存的
 */
int dictDelete(dict *ht, const void *key) {
    return dictGenericDelete(ht,key,0);
}

/*
 * dictGenericDeletey一层调用包装 不清除key和value内存 暂未使用
 */
int dictDeleteNoFree(dict *ht, const void *key) {
    return dictGenericDelete(ht,key,1);
}

/**
 * 销毁整个hashtable的方法
 * @param ht
 * @return
 */
int _dictClear(dict *ht)
{
    unsigned long i;

    /* Free all the elements */
    // 开始遍历所有的节点
    // hashtable的遍历需要遍历整个数组以及数组上面的链表
    // 一层遍历数组
    for (i = 0; i < ht->size && ht->used > 0; i++) {
        dictEntry *he, *nextHe;

        if ((he = ht->table[i]) == NULL) continue;
        // 二层遍历链表
        while(he) {
            nextHe = he->next;
            // 释放key value内存
            dictFreeEntryKey(ht, he);
            dictFreeEntryVal(ht, he);
            // 释放entry内存
            _dictFree(he);
            ht->used--;
            he = nextHe;
        }
    }
    // 释放整个table的内存
    /* Free the table and the allocated cache structure */
    _dictFree(ht->table);
    /* Re-initialize the table */
    // 然后重置hashmap 所有属性值为初始值
    _dictReset(ht);
    return DICT_OK; /* never fails */
}

/*
 * 销毁整个hashmap
 */
/* Clear & Release the hash table */
void dictRelease(dict *ht)
{
    // 释放map上的所有entry 以及table内存
    _dictClear(ht);
    // 释放hashtable内存
    _dictFree(ht);
}

/**
 * 在hashtable中通过key获取对应的entry
 * @param ht
 * @param key
 * @return
 */
dictEntry *dictFind(dict *ht, const void *key)
{
    dictEntry *he;
    unsigned int h;

    if (ht->size == 0) return NULL;
    // 计算所以
    h = dictHashKey(ht, key) & ht->sizemask;
    he = ht->table[h];
    // 比对链表上的所有entry如果相同则返回对应的entry
    while(he) {
        if (dictCompareHashKeys(ht, key, he->key))
            return he;
        he = he->next;
    }
    return NULL;
}

/**
 * 获取hashtable的迭代器
 * @param ht
 * @return
 */
dictIterator *dictGetIterator(dict *ht)
{
    // 分配迭代器内存
    dictIterator *iter = _dictAlloc(sizeof(*iter));
    // 迭代器初始化
    iter->ht = ht;
    iter->index = -1;
    iter->entry = NULL;
    iter->nextEntry = NULL;
    return iter;
}

/**
 * 迭代器的next方法
 * @param iter
 * @return
 */
dictEntry *dictNext(dictIterator *iter)
{

    // 整个hashtable的遍历方向是 从左到右从上到下
    // [][][][][1][][3][][][][]
    //         [2]  [4]
    //              [5]
    while (1) {
        // 如果entry == NULL 数组指针右移
        if (iter->entry == NULL) {
            iter->index++;
            // 如果超过数组长度则 break
            if (iter->index >= (signed)iter->ht->size) break;
            // 迭代器的entry指针指向hashtable索引位置的entry
            iter->entry = iter->ht->table[iter->index];
        } else {
            // 链表指针后移
            iter->entry = iter->nextEntry;
        }
        // 保存entrey的下一个节点然后返回当前节点
        if (iter->entry) {
            /* We need to save the 'next' here, the iterator user
             * may delete the entry we are returning. */
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }
    return NULL;
}

// 释放迭代器的内存
void dictReleaseIterator(dictIterator *iter)
{
    _dictFree(iter);
}

/* Return a random entry from the hash table. Useful to
 * implement randomized algorithms */
/**
 * 从hashtable里面获取一个随机的entry 这个在redis里有啥应用场景嘛
 * 采样？ lua? 惰性删除？
 * @param ht
 * @return
 */
dictEntry *dictGetRandomKey(dict *ht)
{
    dictEntry *he;
    unsigned int h;
    int listlen, listele;

    if (ht->used == 0) return NULL;
    do {
        h = random() & ht->sizemask;
        he = ht->table[h];
    } while(he == NULL);

    /* Now we found a non empty bucket, but it is a linked
     * list and we need to get a random element from the list.
     * The only sane way to do so is to count the element and
     * select a random index. */
    listlen = 0;
    while(he) {
        he = he->next;
        listlen++;
    }
    listele = random() % listlen;
    he = ht->table[h];
    while(listele--) he = he->next;
    return he;
}

/* ------------------------- private functions ------------------------------ */

/* Expand the hash table if needed */
static int _dictExpandIfNeeded(dict *ht)
{
    /* If the hash table is empty expand it to the intial size,
     * if the table is "full" dobule its size. */
    if (ht->size == 0)
        return dictExpand(ht, DICT_HT_INITIAL_SIZE);
    if (ht->used == ht->size)
        return dictExpand(ht, ht->size*2);
    return DICT_OK;
}

/**
 * 这个方法是将容量向上调整为2的幂次方
 * @param size
 * @return
 */
/* Our hash table capability is a power of two */
static unsigned long _dictNextPower(unsigned long size)
{
    unsigned long i = DICT_HT_INITIAL_SIZE;

    if (size >= LONG_MAX) return LONG_MAX;
    while(1) {
        if (i >= size)
            return i;
        i *= 2;
    }
}

/* Returns the index of a free slot that can be populated with
 * an hash entry for the given 'key'.
 * If the key already exists, -1 is returned. */
/**
 * 这个方法用来根据key获得在数组table中的索引
 * @param ht
 * @param key
 * @return
 */
static int _dictKeyIndex(dict *ht, const void *key)
{
    unsigned int h;
    dictEntry *he;

    /* Expand the hashtable if needed */
    // hashtable的容量都是在插入数据的时候确定
    // 所以在查找之前会判断是否需要扩容
    // 1.当hashtable刚刚创建的时候 容量为0时会默认大小4
    // 2.当容量已经用完时在这里会进行扩容调用dictExpand方法
    if (_dictExpandIfNeeded(ht) == DICT_ERR)
        return -1;
    // 计算hash以及与运算取模获取索引
    /* Compute the key hash value */
    h = dictHashKey(ht, key) & ht->sizemask;
    /* Search if this slot does not already contain the given key */
    // 如果当前位置已经有值则还需要比对链表上的每一个节点才能知道key是否存在
    // 因为hash存在并不到表key一定存在
    he = ht->table[h];
    while(he) {
        if (dictCompareHashKeys(ht, key, he->key))
            return -1;
        he = he->next;
    }
    return h;
}

void dictEmpty(dict *ht) {
    _dictClear(ht);
}

#define DICT_STATS_VECTLEN 50
void dictPrintStats(dict *ht) {
    unsigned long i, slots = 0, chainlen, maxchainlen = 0;
    unsigned long totchainlen = 0;
    unsigned long clvector[DICT_STATS_VECTLEN];

    if (ht->used == 0) {
        printf("No stats available for empty dictionaries\n");
        return;
    }

    for (i = 0; i < DICT_STATS_VECTLEN; i++) clvector[i] = 0;
    for (i = 0; i < ht->size; i++) {
        dictEntry *he;

        if (ht->table[i] == NULL) {
            clvector[0]++;
            continue;
        }
        slots++;
        /* For each hash entry on this slot... */
        chainlen = 0;
        he = ht->table[i];
        while(he) {
            chainlen++;
            he = he->next;
        }
        clvector[(chainlen < DICT_STATS_VECTLEN) ? chainlen : (DICT_STATS_VECTLEN-1)]++;
        if (chainlen > maxchainlen) maxchainlen = chainlen;
        totchainlen += chainlen;
    }
    printf("Hash table stats:\n");
    printf(" table size: %ld\n", ht->size);
    printf(" number of elements: %ld\n", ht->used);
    printf(" different slots: %ld\n", slots);
    printf(" max chain length: %ld\n", maxchainlen);
    printf(" avg chain length (counted): %.02f\n", (float)totchainlen/slots);
    printf(" avg chain length (computed): %.02f\n", (float)ht->used/slots);
    printf(" Chain length distribution:\n");
    for (i = 0; i < DICT_STATS_VECTLEN-1; i++) {
        if (clvector[i] == 0) continue;
        printf("   %s%ld: %ld (%.02f%%)\n",(i == DICT_STATS_VECTLEN-1)?">= ":"", i, clvector[i], ((float)clvector[i]/ht->size)*100);
    }
}

/* ----------------------- StringCopy Hash Table Type ------------------------*/

static unsigned int _dictStringCopyHTHashFunction(const void *key)
{
    return dictGenHashFunction(key, strlen(key));
}

static void *_dictStringCopyHTKeyDup(void *privdata, const void *key)
{
    int len = strlen(key);
    char *copy = _dictAlloc(len+1);
    DICT_NOTUSED(privdata);

    memcpy(copy, key, len);
    copy[len] = '\0';
    return copy;
}

static void *_dictStringKeyValCopyHTValDup(void *privdata, const void *val)
{
    int len = strlen(val);
    char *copy = _dictAlloc(len+1);
    DICT_NOTUSED(privdata);

    memcpy(copy, val, len);
    copy[len] = '\0';
    return copy;
}

static int _dictStringCopyHTKeyCompare(void *privdata, const void *key1,
        const void *key2)
{
    DICT_NOTUSED(privdata);

    return strcmp(key1, key2) == 0;
}

static void _dictStringCopyHTKeyDestructor(void *privdata, void *key)
{
    DICT_NOTUSED(privdata);

    _dictFree((void*)key); /* ATTENTION: const cast */
}

static void _dictStringKeyValCopyHTValDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    _dictFree((void*)val); /* ATTENTION: const cast */
}

dictType dictTypeHeapStringCopyKey = {
    _dictStringCopyHTHashFunction,        /* hash function */
    _dictStringCopyHTKeyDup,              /* key dup */
    NULL,                               /* val dup */
    _dictStringCopyHTKeyCompare,          /* key compare */
    _dictStringCopyHTKeyDestructor,       /* key destructor */
    NULL                                /* val destructor */
};

/* This is like StringCopy but does not auto-duplicate the key.
 * It's used for intepreter's shared strings. */
dictType dictTypeHeapStrings = {
    _dictStringCopyHTHashFunction,        /* hash function */
    NULL,                               /* key dup */
    NULL,                               /* val dup */
    _dictStringCopyHTKeyCompare,          /* key compare */
    _dictStringCopyHTKeyDestructor,       /* key destructor */
    NULL                                /* val destructor */
};

/* This is like StringCopy but also automatically handle dynamic
 * allocated C strings as values. */
dictType dictTypeHeapStringCopyKeyValue = {
    _dictStringCopyHTHashFunction,        /* hash function */
    _dictStringCopyHTKeyDup,              /* key dup */
    _dictStringKeyValCopyHTValDup,        /* val dup */
    _dictStringCopyHTKeyCompare,          /* key compare */
    _dictStringCopyHTKeyDestructor,       /* key destructor */
    _dictStringKeyValCopyHTValDestructor, /* val destructor */
};
