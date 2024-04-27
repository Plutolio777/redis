/*
 * 基本的tcp socket方法
 * anet.c -- Basic TCP socket stuff made a bit less boring
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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#include "anet.h"
/**
 * 打印错误日志的方法
 * @param err
 * @param fmt
 * @param ...
 */
static void anetSetError(char *err, const char *fmt, ...)
{
    va_list ap;

    if (!err) return;
    va_start(ap, fmt);
    vsnprintf(err, ANET_ERR_LEN, fmt, ap);
    va_end(ap);
}

/**
 * 将socket设置成非阻塞模式
 * @param err
 * @param fd
 * @return
 */
int anetNonBlock(char *err, int fd)
{
    int flags;

    /* Set the socket nonblocking.
     * Note that fcntl(2) for F_GETFL and F_SETFL can't be
     * interrupted by a signal. */
    // int fcntl(int fd, int cmd, ... /* arg */ ); 用于对socket文件描述符进行对应的操作
    // 1.复制文件描述符(F_DUPFD、int socket) 需要传入需要被复制的描述符
    // 2.F_GETFD、F_SETFD 获取和设置文件描述符标志

    // 获取文件描述符标志
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        anetSetError(err, "fcntl(F_GETFL): %s\n", strerror(errno));
        return ANET_ERR;
    }
    // 设置文件描述符标志位非阻塞
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        anetSetError(err, "fcntl(F_SETFL,O_NONBLOCK): %s\n", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

// 开启TCPNODELAY
int anetTcpNoDelay(char *err, int fd)
{
    int yes = 1;
    // 这里的目的是为了解决发送tcp包的时候会有的一个延时
    // 可以了解一下  Nagle‘s Algorithm 和 TCP Delayed Acknoledgement
    // 这两个算法在一起会有一些副作用导致发包会有40ms左右的延时 这对于redis来说是无法接受的 因此这里关闭了这种机制
    // 这两个算法的目的是为了提高带宽利用率而实现的算法 它的做法是将很多个小的TCP包合并成一个
    // TCP Delayed Acknoledgement 它的作用是合并多个ACK请求 减小带宽压力
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1)
    {
        anetSetError(err, "setsockopt TCP_NODELAY: %s\n", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}
/**
 * 设置发送缓冲区
 * setsockopt
 * 第一个参数表示套接字
 * 第二个参数level表示设置的级别 SOL_SOCKET 表示套接字级别
 * SO_SNDBUF 表示操作名称
 * 其他的还有比如
 *      SO_DEBUG，打开或关闭调试信息
 *      SO_REUSEADDR，打开或关闭地址复用功能
 *      SO_DONTROUTE，打开或关闭路由查找功能
 *      SO_BROADCAST，允许或禁止发送广播数据。
 *      SO_SNDBUF，设置发送缓冲区的大小。
 *      SO_RCVBUF，设置接收缓冲区的大小。
 *      SO_KEEPALIVE，套接字保活
 *      SO_OOBINLINE，紧急数据放入普通数据流。
 *      SO_NO_CHECK，打开或关闭校验和。
 * @param err
 * @param fd
 * @return
 */
int anetSetSendBuffer(char *err, int fd, int buffsize)
{
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffsize, sizeof(buffsize)) == -1)
    {
        anetSetError(err, "setsockopt SO_SNDBUF: %s\n", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}
/**
 * 设置tcp keepalive
 * keepalive是tcp提供的一种机制默认是不开启的 用于在tcp层面自动检测连接方的存活状态
 * @param err
 * @param fd
 * @return
 */
int anetTcpKeepAlive(char *err, int fd)
{
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)) == -1) {
        anetSetError(err, "setsockopt SO_KEEPALIVE: %s\n", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

/**
 * 解析输入的host是否正确 并对host进行域名解析
 * @param err 接收错误信息的变量
 * @param host 主机名
 * @param ipbuf 如果正确解析host会把对应的ip保存到这个变量
 * @return
 */
int anetResolve(char *err, char *host, char *ipbuf)
{
    // struct sockaddr和struct sockaddr_in这两个结构体用来表示网络通信的地址。
    // struct sockaddr {
    //     sa_family_t sin_family; //地址族
    //　　  char sa_data[14];  //14字节，包含套接字中的目标地址和端口信息
    //　};
    // sockaddr是将ip和端口都放到了一个数组中
    // sockaddr_in对该结构进行了优化
    // struct sockaddr_in {
    //    sa_family_t sin_fanily // 地址族
    //    uint16_t sin_port //16为TCP/UDP端口号
    //    struct in_addr // 32为ip地址
    //    char sin_zero[8]//暂不使用
    // }
    // 其中 sin_port和sin_addr都必须是网络字节序（NBO），一般可视化的数字都是主机字节序（HBO）。
    // sa.sin_addr采用的是网络字节序 inet_aton 可以进行网络字节序的转换
    struct sockaddr_in sa;
    // 设置地址族 目前redis只支持ipv4
    sa.sin_family = AF_INET;
    // inet_aton 用于将传入的host村容 sin_addr中
    // 如果传入的是ip则则会直接成 如果是域名则需要进行域名解析
    if (inet_aton(host, &sa.sin_addr) == 0) {
        struct hostent *he;
        // 用于根据host获取ip
        he = gethostbyname(host);

        if (he == NULL) {
            anetSetError(err, "can't resolve: %s\n", host);
            return ANET_ERR;
        }
        // 将解析出来的ip保存在sin_addr中
        memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
    }
    // 将sin_addr转换为字符串并保存在ipbuf中
    strcpy(ipbuf,inet_ntoa(sa.sin_addr));
    return ANET_OK;
}


#define ANET_CONNECT_NONE 0
#define ANET_CONNECT_NONBLOCK 1
/**
 * 用于生成一个tcp连接连接到对应的redis服务(客户端的socket生成)
 * @param err 用于保存方法调用产生的异常
 * @param addr 连接地址
 * @param port 连接端口
 * @param flags 标识符 这里用来表示是否开启非阻塞模式
 * @return 返回连接使用的套接字socket
 */
static int anetTcpGenericConnect(char *err, char *addr, int port, int flags)
{
    int s, on = 1;
    struct sockaddr_in sa;
    // 创建套接字 传入地址族ipv4
    // domain：指定通信协议族。常用的协议族有AF_INET、AF_UNIX等，对于TCP协议，该字段应为AF_INET（ipv4）或AF_INET6（ipv6）。
    // type：指定socket类型。常用的socket类型有SOCK_STREAM（TCP）、SOCK_DGRAM（UDP）等
    // protocol：指定socket所使用的协议，一般我们平常都指定为0，使用type中的默认协议。严格意义上，IPPROTO_TCP（值为6）代表TCP协议。
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        anetSetError(err, "creating socket: %s\n", strerror(errno));
        return ANET_ERR;
    }
    /* Make sure connection-intensive things like the redis benckmark
     * will be able to close/open sockets a zillion of times */
    // 打开端口复用
    // 为什么要开启端口复用
    // 1. 服务器启动后，有客户端连接并已建立，如果服务器主动关闭，那么和客户端的连接会处于TIME_WAIT状态，
    // 此时再次启动服务器，就会bind不成功，报：Address already in use。
    // 2.服务器父进程监听客户端，当和客户端建立链接后，fork一个子进程专门处理客户端的请求，如果父进程停止，因为子进程还和客户端有连接，
    // 所以再次启动父进程，也会报Address already in use。
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    // 设置sockaddr_in的地址族
    sa.sin_family = AF_INET;
    // 设置端口 htons会将port转换成网络字节序
    sa.sin_port = htons(port);
    // 将addr按照网络字节序保存到 sin_addr中 如果失败则需要进行域名解析
    if (inet_aton(addr, &sa.sin_addr) == 0) {
        struct hostent *he;
        // 域名解析的流程
        he = gethostbyname(addr);
        if (he == NULL) {
            anetSetError(err, "can't resolve: %s\n", addr);
            close(s);
            return ANET_ERR;
        }
        memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
    }
    // 开启非阻塞模式
    if (flags & ANET_CONNECT_NONBLOCK) {
        if (anetNonBlock(err,s) != ANET_OK)
            return ANET_ERR;
    }
    // 跟服务端建立连接
    // 第一个参数为socket，当前调用方创建的socket
    // 服务器端的地址
    // *addr的长度。
    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        if (errno == EINPROGRESS &&
            flags & ANET_CONNECT_NONBLOCK)
            return s;

        anetSetError(err, "connect: %s\n", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    return s;
}

// 下面两个函数是anetTcpGenericConnect的包装，一个表示阻塞socket一个是非阻塞socket
int anetTcpConnect(char *err, char *addr, int port)
{
    return anetTcpGenericConnect(err,addr,port,ANET_CONNECT_NONE);
}

int anetTcpNonBlockConnect(char *err, char *addr, int port)
{
    return anetTcpGenericConnect(err,addr,port,ANET_CONNECT_NONBLOCK);
}

/* Like read(2) but make sure 'count' is read before to return
 * (unless error or EOF condition is encountered) */
/**
 * 从socket中读取数据
 * @param fd 套接字
 * @param buf 用于保存socket中数据的buffer
 * @param count 需要读取的数据长度
 * @return 返回读取是否成功
 */
int anetRead(int fd, char *buf, int count)
{
    int nread, totlen = 0;
    // 如果已读数据大小不等于逾期的大小则继续读取
    while(totlen != count) {
        // 调用read方法从接收缓冲区中读取数据
        // 第一个参数为套接字
        // 第二个参数为接收缓冲区
        // 第三个参数为读取的数据长度
        // 返回值0为数据全部读取完毕
        // 返回值为-1表示读取失败
        nread = read(fd,buf,count-totlen);
        // 读取成功则返回已读数据大小
        if (nread == 0) return totlen;
        // 读取失败则返回-1
        if (nread == -1) return -1;
        totlen += nread;
        buf += nread;
    }
    return totlen;
}

/* Like write(2) but make sure 'count' is read before to return
 * (unless error is encountered) */
/**
 * 从socket中写入数据
 * 这个方法也是调用write但是会确保发送的数据大小跟传入的count一致
 * @param fd socket
 * @param buf 发送缓冲区
 * @param count 发送的数据大小
 * @return
 */
int anetWrite(int fd, char *buf, int count)
{
    int nwritten, totlen = 0;
    while(totlen != count) {
        nwritten = write(fd,buf,count-totlen);
        if (nwritten == 0) return totlen;
        if (nwritten == -1) return -1;
        totlen += nwritten;
        buf += nwritten;
    }
    return totlen;
}

/**
 * 接力一个socket客户端
 * @param err
 * @param port
 * @param bindaddr
 * @return
 */
int anetTcpServer(char *err, int port, char *bindaddr)
{
    int s, on = 1;
    struct sockaddr_in sa;
    // 创建socket
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        anetSetError(err, "socket: %s\n", strerror(errno));
        return ANET_ERR;
    }
    // 开启端口复用
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
        anetSetError(err, "setsockopt SO_REUSEADDR: %s\n", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    // sa的第0为赋值为sa的大小
    memset(&sa, 0, sizeof(sa));
    // 设置ipv地址族
    sa.sin_family = AF_INET;
    // 设置端口 网络字节序
    sa.sin_port = htons(port);
    // INADDR_ANY 表示监听0.0.0.0 表示默认监听
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    // 如果传入了地址则改为监听该地址
    if (bindaddr) {
        if (inet_aton(bindaddr, &sa.sin_addr) == 0) {
            anetSetError(err, "Invalid bind address\n");
            close(s);
            return ANET_ERR;
        }
    }
    // 将监听地址绑定到socket
    if (bind(s, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        anetSetError(err, "bind: %s\n", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    // 开始监听
    // 作者说这个511是从nginx中抄过来的 他也不知道干嘛的？？？？
    if (listen(s, 511) == -1) { /* the magic 511 constant is from nginx */
        anetSetError(err, "listen: %s\n", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    return s;
}

/**
 * socket中的accept的封装
 * @param err 用于保存该方法中产生的异常
 * @param serversock 服务端的socket
 * @param ip 用于保存客户端的地址
 * @param port 用于保存客户端的端口
 * @return 返回客户端的socket
 */
int anetAccept(char *err, int serversock, char *ip, int *port)
{
    int fd;
    struct sockaddr_in sa;
    unsigned int saLen;

    while(1) {
        saLen = sizeof(sa);
        fd = accept(serversock, (struct sockaddr*)&sa, &saLen);
        if (fd == -1) {
            if (errno == EINTR)
                continue;
            else {
                anetSetError(err, "accept: %s\n", strerror(errno));
                return ANET_ERR;
            }
        }
        break;
    }
    if (ip) strcpy(ip,inet_ntoa(sa.sin_addr));
    if (port) *port = ntohs(sa.sin_port);
    return fd;
}
