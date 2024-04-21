/* String -> String Map data structure optimized for size.
 * This file implements a data structure mapping strings to other strings
 * implementing an O(n) lookup data structure designed to be very memory
 * efficient.
 * 字符串 -> 字符串映射数据结构针对大小进行了优化。
 * 该文件实现了一个将字符串映射到其他字符串的数据结构，
 * 该数据结构实现了 O(n) 查找数据结构，旨在提高内存效率。
 *
 *
 * redis如果数据比较少的时候采用这种数据结构 目的是为了节省内存
 * 但是zipmap的查询效率是O(n) 当entry的个数打到一定条件的时候会自动转换成hashmap
 * The Redis Hash type uses this data structure for hashes composed of a small
 * number of elements, to switch to an hash table once a given number of
 * elements is reached.
 *
 * redis自己也说了由于hash在存储少量的数据没有必要使用hash占用过量的内存资源 所以这个数据结构的引入是一个重大的胜利
 * Given that many times Redis Hashes are used to represent objects composed
 * of few fields, this is a very big win in terms of used memory.
 *
 * --------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

/* Memory layout of a zipmap, for the map "foo" => "bar", "hello" => "world":
 *
 * zipmap是一个连续的数组内存结构 其内存布局如下
 * [status] ||[key len][key][value len][value free]||[key len][key][value len][value free]........
 * <status><len>"foo"<len><free>"bar"<len>"hello"<len><free>"world"
 * status是一个字节八个比特位 如果最低位为1则说明需要进行内存清理
 * <status> is 1 byte status. Currently only 1 bit is used: if the least
 * significant bit is set, it means the zipmap needs to be defragmented.
 * len表示了接下来的key或者value的长度
 * <len> is the length of the following string (key or value).
 * len的长度是单值或者5字节 说明len这个数据为了优化内存时一个变长的数据结构
 *
 * <len> lengths are encoded in a single value or in a 5 bytes value.
 * 如果len的长度小于252的话 就是会用 {unsigned char}
 * 为啥是252内因为 253表示采用后面追加的四字节{unsigned integer}来表示长度
 * 255 的话表示zipmap的结束
 * 254表示可以用来添加新键值对的空白空间
 * 如果大于253 会追加四个字节的 {unsigned integer}
 *
 * If the first byte value (as an unsigned 8 bit value) is between 0 and
 * 252, it's a single-byte length. If it is 253 then a four bytes unsigned
 * integer follows (in the host byte ordering). A value fo 255 is used to
 * signal the end of the hash. The special value 254 is used to mark
 * empty space that can be used to add new key/value pairs.
 *
 * free表示空闲未使用空间的长度 因为比如第一次设置了长度为5的value后面再设置长度为2的value则会产生空闲空间
 * <free> is the number of free unused bytes
 * after the string, resulting from modification of values associated to a
 * key (for instance if "foo" is set to "bar', and later "foo" will be se to
 * "hi", I'll have a free byte to use if the value will enlarge again later,
 * or even in order to add a key/value pair if it fits.
 *
 * // free的大小永远是一个字节无符号整数0-244
 * 如果一次更新操作之后如果有很多的空白空间则会被标记为254
 * <free> is always an unsigned 8 bit number, because if after an
 * update operation there are more than a few free bytes, they'll be converted
 * into empty space prefixed by the special value 254.
 *
 * The most compact representation of the above two elements hash is actually:
 *
 * "\x00\x03foo\x03\x00bar\x05hello\x05\x00world\xff"
 * // 空白取余使用254字节加一个len来表示
 * 例如上面的结构如果删除 则会表示为254 这意思是删除key空间不会释放而是先标记为254以防后面有数据插入需要重新分配内存
 * 这len会转换成空白空间的大小
 * Empty space is marked using a 254 bytes + a <len> (coded as already
 * specified). The length includes the 254 bytes in the count and the
 * space taken by the <len> field. So for instance removing the "foo" key
 * from the zipmap above will lead to the following representation:
 *
 * "\x00\xfd\x10........\x05hello\x05\x00world\xff"
 *
 * Note that because empty space, keys, values, are all prefixed length
 * "objects", the lookup will take O(N) where N is the numeber of elements
 * in the zipmap and *not* the number of bytes needed to represent the zipmap.
 * This lowers the constant times considerably.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "zmalloc.h"

#define ZIPMAP_BIGLEN 253
#define ZIPMAP_EMPTY 254
#define ZIPMAP_END 255

#define ZIPMAP_STATUS_FRAGMENTED 1

/* The following defines the max value for the <free> field described in the
 * comments above, that is, the max number of trailing bytes in a value. */
#define ZIPMAP_VALUE_MAX_FREE 5

// 这个宏函数定义了如何辨别len的长度是一个字节还是
/* The following macro returns the number of bytes needed to encode the length
 * for the integer value _l, that is, 1 byte for lengths < ZIPMAP_BIGLEN and
 * 5 bytes for all the other lengths. */
#define ZIPMAP_LEN_BYTES(_l) (((_l) < ZIPMAP_BIGLEN) ? 1 : sizeof(unsigned int)+1)


/**
 * 创建一个空的map 实际上就是创建了两个字节的数组
 * 第一位表示status
 * 第二位是一个len 但是值为255代表着map的结束
 * @return 返回创建的map
 */
unsigned char *zipmapNew(void) {
    unsigned char *zm = zmalloc(2);

    zm[0] = 0; /* Status */
    zm[1] = ZIPMAP_END;
    return zm;
}

/**
 * 用于获取真正长度的
 * @param p
 * @return
 */
static unsigned int zipmapDecodeLength(unsigned char *p) {
    unsigned int len = *p;
    // 如果长度小于253的话就直接返回
    if (len < ZIPMAP_BIGLEN) return len;
    // 否则指针移到下一位转换成无符号四字节整数
    memcpy(&len,p+1,sizeof(unsigned int));
    return len;
}

/**
 * 计算长度并保存在对应的len位置，最后返回的是len的字节长度
 * 将传入的len进行编码保存在p开始的1个字节位置或者5个字节位置
 * 如果p为NULL则仅返回len需要的字节长度
 * @param p 这个是len的指针
 * @param len 长度
 * @return 返回字节长度
 */
/* Encode the length 'l' writing it in 'p'. If p is NULL it just returns
 * the amount of bytes required to encode such a length. */
static unsigned int zipmapEncodeLength(unsigned char *p, unsigned int len) {
    // 如果p为空的话就值借返回一个 对应的字节长度
    // 如果大于253则返回1 否则返回5
    if (p == NULL) {
        return ZIPMAP_LEN_BYTES(len);
    } else {
        // 如果小于253则直接将长度保存在p中并返回1字节
        if (len < ZIPMAP_BIGLEN) {
            p[0] = len;
            return 1;
        // 如果大于253 则len的位置保存253 并前将长度保存在len+1后面的四个字节内存中
        // 返回5字节
        } else {
            p[0] = ZIPMAP_BIGLEN;
            memcpy(p+1,&len,sizeof(len));
            return 1+sizeof(len);
        }
    }
}

/**
 * zipmap的核心方法
 * 在map中搜索key 如果未找到则返回NULL
 *
 * @param zm map对象
 * @param key 待搜索的key
 * @param klen 可以的长度
 * @param totlen 如果totallen不为NULL则如果没找到key的话返回map的长度
 * @param freeoff 如果没找到key 且freelen不为NULL则返回第一个空闲空间的偏移量
 * @param freelen 如果没找到key 且freelen不为NULL则返回第一个空闲空间的大小
 * @return
 */
/* Search for a matching key, returning a pointer to the entry inside the
 * zipmap. Returns NULL if the key is not found.
 *
 * If NULL is returned, and totlen is not NULL, it is set to the entire
 * size of the zimap, so that the calling function will be able to
 * reallocate the original zipmap to make room for more entries.
 *
 * If NULL is returned, and freeoff and freelen are not NULL, they are set
 * to the offset of the first empty space that can hold '*freelen' bytes
 * (freelen is an integer pointer used both to signal the required length
 * and to get the reply from the function). If there is not a suitable
 * free space block to hold the requested bytes, *freelen is set to 0. */
static unsigned char *zipmapLookupRaw(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned int *totlen, unsigned int *freeoff, unsigned int *freelen) {
    // +1是为了跳过map首位的status
    unsigned char *p = zm+1;
    unsigned int l;
    // 用来临时保存用户需要的一个空闲空间大小
    unsigned int reqfreelen = 0; /* initialized just to prevent warning */

    if (freelen) {
        reqfreelen = *freelen;
        *freelen = 0;
        assert(reqfreelen != 0);
    }
    // 开始遍历map
    while(*p != ZIPMAP_END) {
        // 如果p为254 则表示p后面的位置是有一段空闲空间
        if (*p == ZIPMAP_EMPTY) {
            // 指针+1 并获取空闲位置的大小
            l = zipmapDecodeLength(p+1);
            /* if the user want a free space report, and this space is
             * enough, and we did't already found a suitable space... */
            // 如果调用查找的时候 想要得到一个可用空间的情况
            // 比如我查找一个可以的时候不存在 在查找的时候知道这个map中是否有空闲空间{freelen}以及空闲空间的位置{freeoff}
            // 我就可以直接根据这个值去设置value而不需要重新遍历插入新的数据了
            if (freelen && l >= reqfreelen && *freelen == 0) {
                // 将map中的空闲空间保存
                *freelen = l;
                //  这个freeoff为当前这个key的起始位置
                *freeoff = p-zm;
            }
            // 然后直接跳过空闲盘点
            p += l;
            // 然后将status置于1 尝试进行碎片整理
            zm[0] |= ZIPMAP_STATUS_FRAGMENTED;
        } else {
            unsigned char free;

            // 获取真正的长度
            l = zipmapDecodeLength(p);
            // key的长度相等 且memcmp如果相等的话为0所有 !0就是表示key相等
            // 注意这里直接返回的是整个entry哦不是value <key len><key><value len><free><value>|........
            if (l == klen && !memcmp(p+1,key,l)) return p;
            // 如果l的长度不等于k的长度则需要跳过
            // 所以p + zipmapEncodeLength(NULL,l) + l表示直接跳过整个key的位置
            p += zipmapEncodeLength(NULL,l) + l;
            /* Skip the value as well */
            // 计算value的长度
            l = zipmapDecodeLength(p);
            // 继续跳过value的len
            p += zipmapEncodeLength(NULL,l);
            // 继续跳过free的位置
            free = p[0];
            p += l+1+free; /* +1 to skip the free byte */
        }
    }
    // 到这了说明遍历完整个map都没有找到key 传入了totallen则返回整个map的长度
    if (totlen != NULL) *totlen = (unsigned int)(p-zm)+1;
    return NULL;
}

/**
 * 根据传入的key长度和value长度计算出map需要新增的大小
 * 插入的时候会调用
 * @param klen key的长度
 * @param vlen value的长度
 * @return 返回需要的大小
 */
static unsigned long zipmapRequiredLength(unsigned int klen, unsigned int vlen) {
    unsigned int l;
    // +3是表示 key len + value len + free
    l = klen+vlen+3;
    // 如果key或者value都大于等于253还需要各自追加4个字节
    if (klen >= ZIPMAP_BIGLEN) l += 4;
    if (vlen >= ZIPMAP_BIGLEN) l += 4;
    return l;
}

/**
 * 返回一个key的完整长度 包括len的长度和payload的长度
 * @param p
 * @return
 */
/* Return the total amount used by a key (encoded length + payload) */
static unsigned int zipmapRawKeyLength(unsigned char *p) {
    // 传入len的值获取payload的长度
    unsigned int l = zipmapDecodeLength(p);
    // zipmapEncodeLength 获取len的长度
    return zipmapEncodeLength(NULL,l) + l;
}

/**
 * 返回一段value的完整长度 包括len的长度，free的长度和payload + free payload的长度
 * @param p
 * @return
 */
/* Return the total amount used by a value
 * (encoded length + single byte free count + payload) */
static unsigned int zipmapRawValueLength(unsigned char *p) {
    // 先解码len的值
    unsigned int l = zipmapDecodeLength(p);

    unsigned int used;
    // p[used] + 1为free自身的长度以及free空间的长度
    // p的位置为value的起始位置 有两种情况
    // [len][free]<payload...><free....> 如果used为1 则p[used]为free空间的长度 +1为自身长度
    //
    used = zipmapEncodeLength(NULL,l);
    // p[used] 为free payload 的长度
    used += p[used] + 1 + l;
    return used;
}

/**
 * 返回一整个entry的完整长度 包括key的完整长度和value的完整长度
 * 两个长度相加即可
 * @param p
 * @return
 */

/* If 'p' points to a key, this function returns the total amount of
 * bytes used to store this entry (entry = key + associated value + trailing
 * free space if any). */
static unsigned int zipmapRawEntryLength(unsigned char *p) {
    unsigned int l = zipmapRawKeyLength(p);

    return l + zipmapRawValueLength(p+l);
}

/**
 * 往zipmap中设置一个key=value
 * @param zm zipmap
 * @param key 设置的key
 * @param klen key的长度
 * @param val 设置的value
 * @param vlen value的长度
 * @param update update是用来告知外部本次插入是更新还是插入
 * @return
 */
/* Set key to value, creating the key if it does not already exist.
 * If 'update' is not NULL, *update is set to 1 if the key was
 * already preset, otherwise to 0. */
unsigned char *zipmapSet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char *val, unsigned int vlen, int *update) {
    unsigned int oldlen = 0, freeoff = 0, freelen;
    // 计算更新操作需要新增的内存大小
    unsigned int reqlen = zipmapRequiredLength(klen,vlen);
    unsigned int empty, vempty;
    unsigned char *p;
    // 查找可以的时候所需要的空闲空间
    freelen = reqlen;
    if (update) *update = 0;
    // 先尝试找一下这个key
    p = zipmapLookupRaw(zm,key,klen,&oldlen,&freeoff,&freelen);
    // freelen是表示我需要使用的空闲空间大小
    // 如果没有找到的话 oldlen里面存了整个map的长度 freeoff里面存了第一个满足要求的空闲空间的地址 freelen里面存了第一个空闲空间的大小
    // 如果没有找到的并且map里的空闲空间也不够 则需要重新分配内存
    if (p == NULL && freelen == 0) {
        /* Key not found, and not space for the new key. Enlarge */
        // 重新分配内存大小
        zm = zrealloc(zm,oldlen+reqlen);
        // p的指针移到最后一位-1 oldlen是map的完整个大小 因为最后一位是代表结束符的len所以直接用就好了
        p = zm+oldlen-1;
        // 最后一位设置一下map结束标志
        zm[oldlen+reqlen-1] = ZIPMAP_END;

        freelen = reqlen;
    // 没有找到key 但是有足够的剩余空间
    }
    else if (p == NULL) {
        // 直接更具free偏移量定位 free空间的位置
        /* Key not found, but there is enough free space. */
        p = zm+freeoff;
        /* note: freelen is already set in this case */
    // 可以存在
    }
    else {
        //               b,p
        //                |
        // 保存一下key的指针 V<len><....><key><len><....><free><value>
        unsigned char *b = p;

        /* Key found. Is there enough space for the new value? */
        /* Compute the total length: */
        if (update) *update = 1;
        // 获取完整的key长度
        freelen = zipmapRawKeyLength(b);
        // 指针跳过整个key
        //                p                b
        //                |                |
        // 保存一下key的指针 V<len><....><key>V<len><....><free><value>
        b += freelen;
        // 计算整个value的长度 此时freelen保存了从p到entry结尾的完整长度
        freelen += zipmapRawValueLength(b);
        // 原来的长度小于需要的长度
        if (freelen < reqlen) {
            /* Mark this entry as free and recurse */
            // 将这个key直接弃用 所以直接标记len为empty
            //                p                b
            //                |                |
            // 保存一下key的指针 V<254><....><key>V<len><....><free><value>
            p[0] = ZIPMAP_EMPTY;
            // p的下一位直接当做free使用
            // 将这个key直接弃用 所以直接标记len为empty
            //                p                b
            //                |                |
            // 保存一下key的指针 V<254><free><...>V<...><....><...><...><下一个entry的len>
            //                            < ------   freelen   ----- >
            zipmapEncodeLength(p+1,freelen);
            // 标记zipmap需要进行压缩
            zm[0] |= ZIPMAP_STATUS_FRAGMENTED;
            // 这里表示这一次发现了key但是可用空间不够 直接弃置这个key然后再递归找下一个
            // 因为这是一个map所以下一次递归应该直接就是设置在map的尾部
            return zipmapSet(zm,key,klen,val,vlen,NULL);
        }
    }
    // 能走到这里只有两中情况
    // 一种是p指针在map的尾部
    // p
    // |
    // V<原结尾符len><..............><新结尾符len>
    // 一种是在空闲空间起始key的位置
    // p
    // |
    // V<len><...><key><len><...><free><原value><下一个key的len>
    /* Ok we have a suitable block where to write the new key/value
     * entry. */
    empty = freelen-reqlen;
    /* 如果有太多可用空间，则将其标记为空闲块
     * 如我们所愿，将其添加为值的尾随空白
     * zipmaps 非常节省空间。 */
    if (empty > ZIPMAP_VALUE_MAX_FREE) {
        unsigned char *e;
        //e指针移到自己保存value够用的位置
        // p                                      e
        // |                                      |
        // V<len><...><key><len><...><free><......V.....><下一个key的len>
        // <                reqlen               > 其他free空间给标记成空闲可用空间
        e = p+reqlen;
        //e指针移到自己保存value够用的位置
        // p                                      e
        // |                                       |
        // V<len><...><key><len><...><free><.......V<254><free><........><下一个key的len>
        // <                reqlen                > 其他free空间给标记成空闲可用空间
        e[0] = ZIPMAP_EMPTY;
        zipmapEncodeLength(e+1,empty);
        vempty = 0;
        // 标记为可以清理
        zm[0] |= ZIPMAP_STATUS_FRAGMENTED;
    } else {
        vempty = empty;
    }
    // 如果空闲空间不是很多 则vempty保存赋值后空闲空间的大小
    /* Just write the key + value and we are done. */
    /* Key: */
    // 将klen编码进len的位置然后跳过

    p += zipmapEncodeLength(p,klen);
    // 直接覆盖掉key
    memcpy(p,key,klen);
    // 跳过key payload段 进入value段
    p += klen;
    // 将value长度编码进 value len段
    /* Value: */
    p += zipmapEncodeLength(p,vlen);
    // 指针后移一位 赋free的值
    *p++ = vempty;
    // 写入value
    memcpy(p,val,vlen);
    return zm;
}

/**
 *
 * 从zm中删除一个key 删除实际上就是将这个位置变为空闲位置或被压缩 或被重写 实际上内存大小并不一定会改变
 * @param zm 对应的zipmap
 * @param key 删除的key
 * @param klen 删除的key长度
 * @param deleted 告知外界是否key没有找到 如果为0在表示没有这个key
 * @return
 */
/* Remove the specified key. If 'deleted' is not NULL the pointed integer is
 * set to 0 if the key was not found, to 1 if it was found and deleted. */
unsigned char *zipmapDel(unsigned char *zm, unsigned char *key, unsigned int klen, int *deleted) {
    // 调用这个方法查找key
    unsigned char *p = zipmapLookupRaw(zm,key,klen,NULL,NULL,NULL);
    // 找到key了
    if (p) {
        // 获取整个entry的长度 这个长度就是后续空闲长度
        unsigned int freelen = zipmapRawEntryLength(p);
        // key len位置设置成空
        p[0] = ZIPMAP_EMPTY;
        // p+1的位置当free用并编码进freelen
        zipmapEncodeLength(p+1,freelen);
        // 修改压缩标志
        zm[0] |= ZIPMAP_STATUS_FRAGMENTED;
        if (deleted) *deleted = 1;
    } else {
        if (deleted) *deleted = 0;
    }
    return zm;
}

// 这个方法就是跳过status的位置没啥好说的
/* Call it before to iterate trought elements via zipmapNext() */
unsigned char *zipmapRewind(unsigned char *zm) {
    return zm+1;
}


/* This function is used to iterate through all the zipmap elements.
 * In the first call the first argument is the pointer to the zipmap + 1.
 * In the next calls what zipmapNext returns is used as first argument.
 * Example:
 *
 * unsigned char *i = zipmapRewind(my_zipmap);
 * while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) {
 *     printf("%d bytes key at $p\n", klen, key);
 *     printf("%d bytes value at $p\n", vlen, value);
 * }
 */
/**
 * 用于迭代整个zipmap
 * @param zm 目前的位置
 * @param key 用于保存key的值
 * @param klen 用于保存key的长度
 * @param value 用于保存value的值
 * @param vlen 用于保存value的长度
 * @return
 */
unsigned char *zipmapNext(unsigned char *zm, unsigned char **key, unsigned int *klen, unsigned char **value, unsigned int *vlen) {
    // 如果当前指针为表示空 则计算下一位free的长度并直接跳过
    while(zm[0] == ZIPMAP_EMPTY)
        zm += zipmapDecodeLength(zm+1);
    // 如果遍历到结尾符则返回NULL
    if (zm[0] == ZIPMAP_END) return NULL;
    if (key) {
        // 此时zm为len的位置
        *key = zm;
        // 解码len大小
        *klen = zipmapDecodeLength(zm);
        // 然后key指针直接跳过len段
        *key += ZIPMAP_LEN_BYTES(*klen);
    }
    // zm直接跳过整个key
    //          key   zm
    //          |     |
    //<len><...>V<key>V<len><...><free><value>

    zm += zipmapRawKeyLength(zm);
    if (value) {
        // value指针跳过free
        *value = zm+1;
        // 保存value的长度
        *vlen = zipmapDecodeLength(zm);
        // 跳过len
        *value += ZIPMAP_LEN_BYTES(*vlen);
    }
    // zm直接跳过整个key
    //          key   zm             value
    //          |     |                |
    //<len><...>V<key>V<len><...><free>V<value>
    //zm跳过value段 进入下一个entry
    // zm直接跳过整个key
    //          key                 value     zm
    //          |                     |       |
    //<len><...>V<key><len><...><free>V<value>V
    zm += zipmapRawValueLength(zm);
    return zm;
}
/**
 * zipmap的查找方法
 * @param zm 目标zipmap
 * @param key 查找的key
 * @param klen 查找的可以长度
 * @param value 如果找到了则保存在value中
 * @param vlen  如果找到了则把value的长度保存在vlen中
 * @return 是否找到key 0表示没找到 1表示找到了
 */
/* Search a key and retrieve the pointer and len of the associated value.
 * If the key is found the function returns 1, otherwise 0. */
int zipmapGet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char **value, unsigned int *vlen) {
    unsigned char *p;

    // 调用@zipmapLookupRaw查找
    // 因为只是查找不是插入的时候查找 所以totallen freelen freeoff都传NULL即可
    // 如果p==NULL则没有找到 返回0
    if ((p = zipmapLookupRaw(zm,key,klen,NULL,NULL,NULL)) == NULL) return 0;
    // 如果找到了 则p是在整个entry的起始位置 然后需要跳过整个key
    p += zipmapRawKeyLength(p);
    // 将value的长度保存在value中
    *vlen = zipmapDecodeLength(p);
    // 然后跳过len和free直接去value
    *value = p + ZIPMAP_LEN_BYTES(*vlen) + 1;
    return 1;
}

/* Return 1 if the key exists, otherwise 0 is returned. */
/**
 * 用于判断key是否存在
 * @param zm  目标zipmap
 * @param key 查找的key
 * @param klen 查找的key长度
 * @return
 */
int zipmapExists(unsigned char *zm, unsigned char *key, unsigned int klen) {
    // 调用原始方法查找没什么好说的
    return zipmapLookupRaw(zm,key,klen,NULL,NULL,NULL) != NULL;
}

/* Return the number of entries inside a zipmap */
/**
 * 获取zm的元素个数
 * @param zm
 * @return
 */
unsigned int zipmapLen(unsigned char *zm) {
    unsigned char *p = zipmapRewind(zm);
    unsigned int len = 0;
    // 用zipmapNext一个entry一个entry的遍历可以获取长度
    while((p = zipmapNext(p,NULL,NULL,NULL,NULL)) != NULL) len++;
    return len;
}

/**
 * 打印整个zipmap
 * @param p
 */
void zipmapRepr(unsigned char *p) {
    unsigned int l;

    printf("{status %u}",*p++);
    while(1) {
        if (p[0] == ZIPMAP_END) {
            printf("{end}");
            break;
        } else if (p[0] == ZIPMAP_EMPTY) {
            l = zipmapDecodeLength(p+1);
            printf("{%u empty block}", l);
            p += l;
        } else {
            unsigned char e;

            l = zipmapDecodeLength(p);
            printf("{key %u}",l);
            p += zipmapEncodeLength(NULL,l);
            fwrite(p,l,1,stdout);
            p += l;

            l = zipmapDecodeLength(p);
            printf("{value %u}",l);
            p += zipmapEncodeLength(NULL,l);
            e = *p++;
            fwrite(p,l,1,stdout);
            p += l+e;
            if (e) {
                printf("[");
                while(e--) printf(".");
                printf("]");
            }
        }
    }
    printf("\n");
}

#ifdef ZIPMAP_TEST_MAIN
int main(void) {
    unsigned char *zm;

    zm = zipmapNew();

    zm = zipmapSet(zm,(unsigned char*) "name",4, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "surname",7, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "age",3, (unsigned char*) "foo",3,NULL);
    zipmapRepr(zm);
    exit(1);

    zm = zipmapSet(zm,(unsigned char*) "hello",5, (unsigned char*) "world!",6,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "bar",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "!",1,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "12345",5,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "new",3, (unsigned char*) "xx",2,NULL);
    zm = zipmapSet(zm,(unsigned char*) "noval",5, (unsigned char*) "",0,NULL);
    zipmapRepr(zm);
    zm = zipmapDel(zm,(unsigned char*) "new",3,NULL);
    zipmapRepr(zm);
    printf("\nPerform a direct lookup:\n");
    {
        unsigned char *value;
        unsigned int vlen;

        if (zipmapGet(zm,(unsigned char*) "foo",3,&value,&vlen)) {
            printf("  foo is associated to the %d bytes value: %.*s\n",
                vlen, vlen, value);
        }
    }
    printf("\nIterate trought elements:\n");
    {
        unsigned char *i = zipmapRewind(zm);
        unsigned char *key, *value;
        unsigned int klen, vlen;

        while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) {
            printf("  %d:%.*s => %d:%.*s\n", klen, klen, key, vlen, vlen, value);
        }
    }
    return 0;
}
#endif
