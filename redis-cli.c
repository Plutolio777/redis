/* Redis CLI (command line interface)
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

#include "fmacros.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "anet.h"
#include "sds.h"
#include "adlist.h"
#include "zmalloc.h"

#define REDIS_CMD_INLINE 1
#define REDIS_CMD_BULK 2
#define REDIS_CMD_MULTIBULK 4

#define REDIS_NOTUSED(V) ((void) V)

static struct config {
    char *hostip;
    int hostport;
    long repeat;
    int dbnum;
    int interactive;
    char *auth;
} config;

/***
 * redisCommand
 * @arg name 命令名称
 * @arg arity 命令校验的规则 如果大于0则校验参数个数是否相等 如果小于零则校验最小长度
 * @flags 参数类型
 *      @REDIS_CMD_INLINE 单行参数 get key
 *      @REDIS_CMD_BULK 多个参数 setnx key value ttl ...
 *      @REDIS_CMD_MULTIBULK 多组参数 如 hmset key k1 v1 k2 v2 k3 v3
 */
struct redisCommand {
    char *name;
    int arity;
    int flags;
};

/**
 * redisCommand
 * 定义redis中合法的指令以及一些参数校验规则，类型
 * @arg cmdTable
 */
static struct redisCommand cmdTable[] = {
    {"auth",2,REDIS_CMD_INLINE},
    {"get",2,REDIS_CMD_INLINE},
    {"set",3,REDIS_CMD_BULK},
    {"setnx",3,REDIS_CMD_BULK},
    {"append",3,REDIS_CMD_BULK},
    {"substr",4,REDIS_CMD_INLINE},
    {"del",-2,REDIS_CMD_INLINE},
    {"exists",2,REDIS_CMD_INLINE},
    {"incr",2,REDIS_CMD_INLINE},
    {"decr",2,REDIS_CMD_INLINE},
    {"rpush",3,REDIS_CMD_BULK},
    {"lpush",3,REDIS_CMD_BULK},
    {"rpop",2,REDIS_CMD_INLINE},
    {"lpop",2,REDIS_CMD_INLINE},
    {"brpop",-3,REDIS_CMD_INLINE},
    {"blpop",-3,REDIS_CMD_INLINE},
    {"llen",2,REDIS_CMD_INLINE},
    {"lindex",3,REDIS_CMD_INLINE},
    {"lset",4,REDIS_CMD_BULK},
    {"lrange",4,REDIS_CMD_INLINE},
    {"ltrim",4,REDIS_CMD_INLINE},
    {"lrem",4,REDIS_CMD_BULK},
    {"rpoplpush",3,REDIS_CMD_BULK},
    {"sadd",3,REDIS_CMD_BULK},
    {"srem",3,REDIS_CMD_BULK},
    {"smove",4,REDIS_CMD_BULK},
    {"sismember",3,REDIS_CMD_BULK},
    {"scard",2,REDIS_CMD_INLINE},
    {"spop",2,REDIS_CMD_INLINE},
    {"srandmember",2,REDIS_CMD_INLINE},
    {"sinter",-2,REDIS_CMD_INLINE},
    {"sinterstore",-3,REDIS_CMD_INLINE},
    {"sunion",-2,REDIS_CMD_INLINE},
    {"sunionstore",-3,REDIS_CMD_INLINE},
    {"sdiff",-2,REDIS_CMD_INLINE},
    {"sdiffstore",-3,REDIS_CMD_INLINE},
    {"smembers",2,REDIS_CMD_INLINE},
    {"zadd",4,REDIS_CMD_BULK},
    {"zincrby",4,REDIS_CMD_BULK},
    {"zrem",3,REDIS_CMD_BULK},
    {"zremrangebyscore",4,REDIS_CMD_INLINE},
    {"zmerge",-3,REDIS_CMD_INLINE},
    {"zmergeweighed",-4,REDIS_CMD_INLINE},
    {"zrange",-4,REDIS_CMD_INLINE},
    {"zrank",3,REDIS_CMD_BULK},
    {"zrevrank",3,REDIS_CMD_BULK},
    {"zrangebyscore",-4,REDIS_CMD_INLINE},
    {"zcount",4,REDIS_CMD_INLINE},
    {"zrevrange",-4,REDIS_CMD_INLINE},
    {"zcard",2,REDIS_CMD_INLINE},
    {"zscore",3,REDIS_CMD_BULK},
    {"incrby",3,REDIS_CMD_INLINE},
    {"decrby",3,REDIS_CMD_INLINE},
    {"getset",3,REDIS_CMD_BULK},
    {"randomkey",1,REDIS_CMD_INLINE},
    {"select",2,REDIS_CMD_INLINE},
    {"move",3,REDIS_CMD_INLINE},
    {"rename",3,REDIS_CMD_INLINE},
    {"renamenx",3,REDIS_CMD_INLINE},
    {"keys",2,REDIS_CMD_INLINE},
    {"dbsize",1,REDIS_CMD_INLINE},
    {"ping",1,REDIS_CMD_INLINE},
    {"echo",2,REDIS_CMD_BULK},
    {"save",1,REDIS_CMD_INLINE},
    {"bgsave",1,REDIS_CMD_INLINE},
    {"rewriteaof",1,REDIS_CMD_INLINE},
    {"bgrewriteaof",1,REDIS_CMD_INLINE},
    {"shutdown",1,REDIS_CMD_INLINE},
    {"lastsave",1,REDIS_CMD_INLINE},
    {"type",2,REDIS_CMD_INLINE},
    {"flushdb",1,REDIS_CMD_INLINE},
    {"flushall",1,REDIS_CMD_INLINE},
    {"sort",-2,REDIS_CMD_INLINE},
    {"info",1,REDIS_CMD_INLINE},
    {"mget",-2,REDIS_CMD_INLINE},
    {"expire",3,REDIS_CMD_INLINE},
    {"expireat",3,REDIS_CMD_INLINE},
    {"ttl",2,REDIS_CMD_INLINE},
    {"slaveof",3,REDIS_CMD_INLINE},
    {"debug",-2,REDIS_CMD_INLINE},
    {"mset",-3,REDIS_CMD_MULTIBULK},
    {"msetnx",-3,REDIS_CMD_MULTIBULK},
    {"monitor",1,REDIS_CMD_INLINE},
    {"multi",1,REDIS_CMD_INLINE},
    {"exec",1,REDIS_CMD_INLINE},
    {"discard",1,REDIS_CMD_INLINE},
    {"hset",4,REDIS_CMD_MULTIBULK},
    {"hget",3,REDIS_CMD_BULK},
    {"hdel",3,REDIS_CMD_BULK},
    {"hlen",2,REDIS_CMD_INLINE},
    {"hkeys",2,REDIS_CMD_INLINE},
    {"hvals",2,REDIS_CMD_INLINE},
    {"hgetall",2,REDIS_CMD_INLINE},
    {"hexists",3,REDIS_CMD_BULK},
    {NULL,0,0}
};

static int cliReadReply(int fd);
static void usage();

/***
 * lookupCommand
 * 检查用户输入的命令是否符合redis语法
 * @param name 命令名称 如 get,set
 * @return 返回抽象的命令struct @redisCommand 该结构用于校验命令
 */
static struct redisCommand *lookupCommand(char *name) {
    int j = 0;
    while(cmdTable[j].name != NULL) {
        // strcasecmp 忽略大小写的比较
        if (!strcasecmp(name,cmdTable[j].name)) return &cmdTable[j];
        j++;
    }
    return NULL;
}

/**
 * cliConnect
 * 获取redis-server套接字
 * @return
 */
static int cliConnect(void) {
    char err[ANET_ERR_LEN];
    static int fd = ANET_ERR;

    if (fd == ANET_ERR) {
        // 连接redis-server返回socket套接字
        fd = anetTcpConnect(err,config.hostip,config.hostport);
        if (fd == ANET_ERR) {
            fprintf(stderr, "Could not connect to Redis at %s:%d: %s", config.hostip, config.hostport, err);
            return -1;
        }
        anetTcpNoDelay(NULL,fd);
    }
    return fd;
}

/***
 * cliReadLine
 * 读取socket buffer中的消息直到\n
 * 读取redis-server 创建新sds
 * @param fd redis-server socket
 * @return
 */
static sds cliReadLine(int fd) {
    // 分配一个sds内存空间
    sds line = sdsempty();

    while(1) {
        char c;
        ssize_t ret;
        // 从buffer中读取1字节数据放入c中 ret=0表示buffer为空
        ret = read(fd,&c,1);
        if (ret == -1) {
            sdsfree(line);
            return NULL;
            // redis-server返回都是以\n结尾
        } else if ((ret == 0) || (c == '\n')) {
            break;
        } else {
            line = sdscatlen(line,&c,1);
        }
    }
    return sdstrim(line,"\r\n");
}

/***
 * cliReadSingleLineReply
 * 读取redis-server的消息如果成功则打印在控制台返回0 如果失败返回1
 * 如果是quite命令则不需要打印结果
 * @param fd redis-server socket
 * @param quiet 用来控制是否需要将返回打印在控制台 1不打印 0打印
 * @return int 为0表示成功 1读取内容为NULL
 */
static int cliReadSingleLineReply(int fd, int quiet) {
    // 从redis-server读取单行消息
    sds reply = cliReadLine(fd);

    if (reply == NULL) return 1;
    if (!quiet)
        printf("%s\n", reply);
    // 释放sds内存
    sdsfree(reply);
    return 0;
}

/***
 * cliReadBulkReply
 * 读取多个char的消息
 * @param fd
 * @return
 */
static int cliReadBulkReply(int fd) {
    // 当消息头为*时会进入这个方法 *后面跟的是整个参数的长度 所以这里会读取reply长度
    sds replylen = cliReadLine(fd);
    char *reply, crlf[2];
    int bulklen;
    // 如果argc为NULL则返回1表示读取错误
    if (replylen == NULL) return 1;
    // char转为int
    bulklen = atoi(replylen);
    // 表示无参数 打印nil并返回读取成功
    if (bulklen == -1) {
        sdsfree(replylen);
        printf("(nil)\n");
        return 0;
    }
    // 分配argc字节内存用于存放回复数据
    reply = zmalloc(bulklen);
    // 将buffer内容读取到reply中
    anetRead(fd,reply,bulklen);
    anetRead(fd,crlf,2);
    // 将reply中的内容输入到stdout流（会打印到控制台）
    if (bulklen && fwrite(reply,bulklen,1,stdout) == 0) {
        zfree(reply);
        return 1;
    }
    if (isatty(fileno(stdout)) && reply[bulklen-1] != '\n')
        printf("\n");
    // 释放内存
    zfree(reply);
    return 0;
}

static int cliReadMultiBulkReply(int fd) {
    sds replylen = cliReadLine(fd);
    int elements, c = 1;

    if (replylen == NULL) return 1;
    elements = atoi(replylen);
    if (elements == -1) {
        sdsfree(replylen);
        printf("(nil)\n");
        return 0;
    }
    if (elements == 0) {
        printf("(empty list or set)\n");
    }
    while(elements--) {
        printf("%d. ", c);
        if (cliReadReply(fd)) return 1;
        c++;
    }
    return 0;
}


/***
 * cliReadReply
 * 发送消息后处理server的回复消息
 * @param fd server socket
 * @return 返回1表示命令执行失败 0表示命令执行成功
 */
static int cliReadReply(int fd) {
    char type;
    // 从fd socket buffer中读取1位 首位为返回值类型
    if (anetRead(fd,&type,1) <= 0) exit(1);
    printf("接收到的消息类型是%s\n", &type);
    switch(type) {
    // 错误处理
    case '-':
        printf("(error) ");
        //
        cliReadSingleLineReply(fd,0);
        return 1;
    // 读取当行消息 cliReadSingleLineReply 会读取socket中所有的消息直到\n
    // 如 set a1 v1 返回的消息类型是+ 返回的是OK
    case '+':
        return cliReadSingleLineReply(fd,0);
    // 读取单行integer类型的消息
    case ':':
        printf("(integer) ");
        return cliReadSingleLineReply(fd,0);
    // 读取多个消息
    // 如lrange test 0 -1 读取的消息类型是${长度}\r\n{参数}\r\n
    case '$':
        return cliReadBulkReply(fd);
    // 读取多组消息 *后面跟数字其实就是表示后面有几组 $开头的数据需要重复处理
    case '*':
        return cliReadMultiBulkReply(fd);
    default:
        printf("protocol error, got '%c' as reply type byte\n", type);
        return 1;
    }
}

/***
 * selectDb
 * 通过-n指定dbNum时redis-cli帮我们发送 select dbNum选择数据库
 * @param fd @redis-server socket
 * @return 执行结果 1指令执行成功 0表示执行失败
 */
static int selectDb(int fd) {
    int retval;
    sds cmd;
    char type;
    // 初次连接redis-server的初始数据库为0 不需要切换DB 直接返回
    if (config.dbnum == 0)
        return 0;

    // 这里是当我们通过-n来指定db时redis会帮我们发送一个 select dbNum 命令来选择数据库
    // 和我们进入redis-cli之后输入select dbNum是同理
    cmd = sdsempty();
    cmd = sdscatprintf(cmd,"SELECT %d\r\n",config.dbnum);
    anetWrite(fd,cmd,sdslen(cmd));
    anetRead(fd,&type,1);
    if (type <= 0 || type != '+') return 1;
    // 此方法会读取redis执行命令后的结果并打印在控制台
    retval = cliReadSingleLineReply(fd,1);
    if (retval) {
        return retval;
    }
    return 0;
}

/***
 * cliSendCommand
 * 用于想redis-server发送指令
 * @param argc 参数个数
 * @param argv 参数数组
 * @return
 */
static int cliSendCommand(int argc, char **argv) {
    // 获取命令struct信息
    struct redisCommand *rc = lookupCommand(argv[0]);
    int fd, j, retval = 0;
    int read_forever = 0;
    sds cmd;
    // 如果为空则打印command不存在
    if (!rc) {
        fprintf(stderr,"Unknown command '%s'\n",argv[0]);
        return 1;
    }
    // redis命令的校验规则:
    // 如果 @rc.arity 大于零则校验参数个数
    // 如果 @rc.arity 小于零则校验参数的最小长度
    if ((rc->arity > 0 && argc != rc->arity) ||
        (rc->arity < 0 && argc < -rc->arity)) {
        fprintf(stderr,"Wrong number of arguments for '%s'\n",rc->name);
        return 1;
    }
    // 进入redis 监控模式
    if (!strcasecmp(rc->name,"monitor")) read_forever = 1;
    // 建立与redis-server的连接 获取socket
    if ((fd = cliConnect()) == -1) return 1;
    // select db
    retval = selectDb(fd);
    if (retval) {
        fprintf(stderr,"Error setting DB num\n");
        return 1;
    }

    while(config.repeat--) {
        // 组装redis命令 首先创建一个空sds
        cmd = sdsempty();
        // 类似于 mset k1 v1 k2 v2 命令组装 @REDIS_CMD_MULTIBULK 表示多个key多个value
        // 输入mset k1 v1 k2 v2
        //          |
        //          |
        //          V
        // *7\r\n$4mset\r\n$2k1\r\n$2v1\r\n$2k2\r\n$2v2\r\n$2k3\r\n$2v3\r\n
        if (rc->flags & REDIS_CMD_MULTIBULK) {
            // *{参数总个数}\r\n
            cmd = sdscatprintf(cmd,"*%d\r\n",argc);
            for (j = 0; j < argc; j++) {
                // ${参数长度}\r\n{参数字面量}\r\n
                cmd = sdscatprintf(cmd,"$%lu\r\n",
                                   (unsigned long)sdslen(argv[j]));
                cmd = sdscatlen(cmd,argv[j],sdslen(argv[j]));
                cmd = sdscatlen(cmd,"\r\n",2);
            }

        } else {
            // 如果类型为 REDIS_CMD_INLINE 则命令之间用空格隔开如
            // get k1 -----> get k1\r\n
            // 如果类型为REDIS_CMD_BULK 则最后一个参数之前需要加上参数的长度
            // lpush k1 v1 -----> lpush k1 2\r\nv1
            for (j = 0; j < argc; j++) {
                // 如果不为首个则加一个空格
                if (j != 0) cmd = sdscat(cmd," ");
                // 类似于 lpush k v1 v2 v3  命令组装 @REDIS_CMD_BULK 一个key多个value
                // cmd = cmd + {参数长度}
                if (j == argc-1 && rc->flags & REDIS_CMD_BULK) {
                    cmd = sdscatprintf(cmd,"%lu",
                                       (unsigned long)sdslen(argv[j]));
                    // 类似于 get key  命令组装 @REDIS_CMD_INLINE 一个key一个value
                    // cmd = cmd {参数字面量}
                } else {
                    cmd = sdscatlen(cmd,argv[j],sdslen(argv[j]));
                }
            }
            cmd = sdscat(cmd,"\r\n");
            if (rc->flags & REDIS_CMD_BULK) {
                cmd = sdscatlen(cmd,argv[argc-1],sdslen(argv[argc-1]));
                cmd = sdscatlen(cmd,"\r\n",2);
            }
            printf("%s", cmd);
        }
        // 将sds写入socket发送到redis-server
        anetWrite(fd, cmd, sdslen(cmd));
        // 释放sds内存
        sdsfree(cmd);

        // 如果输入monitor进入监控模式 将循环读取redis的内容打印在控制台
        while (read_forever) {
            cliReadSingleLineReply(fd,0);
        }

        retval = cliReadReply(fd);
        if (retval) {
            return retval;
        }
    }
    return 0;
}
/***
 * parseOptions
 * 解析redis启动参数
 * @param argc 参数个数
 * @param argv 参数数组
 * @return
 */
static int parseOptions(int argc, char **argv) {
    int i;

    for (i = 1; i < argc; i++) {
        int lastarg = i==argc-1;

        if (!strcmp(argv[i],"-h") && !lastarg) {
            char *ip = zmalloc(32);
            // 解析-h后的域名是否是正确的域名， 如果正确将值赋给@config->ip
            if (anetResolve(NULL,argv[i+1],ip) == ANET_ERR) {
                printf("Can't resolve %s\n", argv[i]);
                exit(1);
            }
            config.hostip = ip;
            i++;
            // 如果最后一个参数是 -h 打印帮助文档
        } else if (!strcmp(argv[i],"-h") && lastarg) {
            usage();
        } else if (!strcmp(argv[i],"-p") && !lastarg) {
            config.hostport = atoi(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-r") && !lastarg) {
            config.repeat = strtoll(argv[i+1],NULL,10);
            i++;
        } else if (!strcmp(argv[i],"-n") && !lastarg) {
            config.dbnum = atoi(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-a") && !lastarg) {
            config.auth = argv[i+1];
            i++;
            // -i 表示不进入交互模式
        } else if (!strcmp(argv[i],"-i")) {
            config.interactive = 1;
        } else {
            break;
        }
    }
    return i;
}

static sds readArgFromStdin(void) {
    char buf[1024];
    sds arg = sdsempty();

    while(1) {
        int nread = read(fileno(stdin),buf,1024);

        if (nread == 0) break;
        else if (nread == -1) {
            perror("Reading from standard input");
            exit(1);
        }
        arg = sdscatlen(arg,buf,nread);
    }
    return arg;
}

static void usage() {
    fprintf(stderr, "usage: redis-cli [-h host] [-p port] [-a authpw] [-r repeat_times] [-n db_num] [-i] cmd arg1 arg2 arg3 ... argN\n");
    fprintf(stderr, "usage: echo \"argN\" | redis-cli [-h host] [-a authpw] [-p port] [-r repeat_times] [-n db_num] cmd arg1 arg2 ... arg(N-1)\n");
    fprintf(stderr, "\nIf a pipe from standard input is detected this data is used as last argument.\n\n");
    fprintf(stderr, "example: cat /etc/passwd | redis-cli set my_passwd\n");
    fprintf(stderr, "example: redis-cli get my_passwd\n");
    fprintf(stderr, "example: redis-cli -r 100 lpush mylist x\n");
    fprintf(stderr, "\nRun in interactive mode: redis-cli -i or just don't pass any command\n");
    exit(1);
}

/* Turn the plain C strings into Sds strings */
static char **convertToSds(int count, char** args) {
    int j;
    char **sds = zmalloc(sizeof(char*)*count+1);

    for(j = 0; j < count; j++)
        sds[j] = sdsnew(args[j]);

    return sds;
}
/***
 * prompt
 * 用于获取用户输入的内容并返回字符串
 * @param line 输入缓冲区
 * @param size 输入缓冲区最大值
 * @return 返回用户输入的内容
 */
static char *prompt(char *line, int size) {
    char *retval;

    do {
        printf(">> ");

        // 读取输入
        retval = fgets(line, size, stdin);
    } while (retval && *line == '\n');
    line[strlen(line) - 1] = '\0';

    return retval;
}


/***
 * strsep
 * 补充确实的函数
 * @param stringp 传入的字符串
 * @param delim 需要分割的字符
 * @return
 */
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


/***
 * repl
 * redis的交互模式 也就是我们最常使用的输入命令的模式
 */
static void repl() {
    int size = 4096, max = size >> 1, argc;
    char buffer[size];
    char *line = buffer;
    char **ap, *args[max];
    // 如果 -a 则进行密码的校验
    if (config.auth != NULL) {
        char *authargv[2];

        authargv[0] = "AUTH";
        authargv[1] = config.auth;
        cliSendCommand(2, convertToSds(2, authargv));
    }
    // 这里会循环接收用户输入
    while (prompt(line, size)) {
        argc = 0;
        // strsep 使用来进行字符串分割它的原理是 将字符串中的指定字符替换成\0 并循环获取分割的值
        // strsep(&line, " \t")
        //     ['a','b',' ','c','d','\t' ,'d'] ----->
        //     ['a','b']
        //     ['c','d']
        //     ['d']
        //     args是一个数组 ap是数组中的指针 通过改变ap值 将结果赋值给*ap可以
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

        config.repeat = 1;
        cliSendCommand(argc, convertToSds(argc, args));
        line = buffer;
    }

    exit(0);
}

int main(int argc, char **argv) {
    int firstarg;
    char **argvcopy;
    struct redisCommand *rc;

    config.hostip = "127.0.0.1";
    config.hostport = 6379;
    config.repeat = 1;
    config.dbnum = 0;
    config.interactive = 0;
    config.auth = NULL;

    firstarg = parseOptions(argc,argv);
    argc -= firstarg;
    argv += firstarg;
    // parseOptions 返回值为正确解析的参数个数 如果与原始个数相等则表示参数解析无问题 进入交互模式
    if (argc == 0 || config.interactive == 1) repl();

    argvcopy = convertToSds(argc, argv);

    /* Read the last argument from stdandard input if needed */
    if ((rc = lookupCommand(argv[0])) != NULL) {
      if (rc->arity > 0 && argc == rc->arity-1) {
        sds lastarg = readArgFromStdin();
        argvcopy[argc] = lastarg;
        argc++;
      }
    }

    return cliSendCommand(argc, argvcopy);
}
