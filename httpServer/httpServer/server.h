#ifndef SERVER_H
#define SERVER_H

//服务器监听业务逻辑

//初始化监听文件描述符
int initListenFd(unsigned short port);
//启动epoll模型
int epollRun(unsigned short port);
//建立新连接
int acceptConn(int lfd, int epfd);
//接收客户端http请求消息
int recvHttpRequest(int cfd,int epfd);

char* kmp(char* s, char* p);

//解析请求行
int parseRequestLine(const char* reqLine,int cfd);
//发送内容
int sendFile(int cfd,const char* fileName);
//发送头数据（状态行，响应头，空行）
int sendHeadMsg(int cfd,int status, const char* descr, const char* type, int length);
//发送目录
int sendDir(int cfd,const char* dirName);
//和客户端断开连接
int disConnect(int cfd,int epfd);
//获取文件类型
const char* getFileType(const char* filename);

//解码
int hexit(char c);
void decodeMsg(char* to, char* from);

#endif