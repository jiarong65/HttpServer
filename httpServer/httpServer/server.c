#include "server.h"
#include<arpa/inet.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<stdio.h>
#include<errno.h>
#include<sys/stat.h>
#include<stdlib.h>
#include<string.h>
#include<strings.h>
#include<dirent.h>
#include<unistd.h>

int initListenFd(unsigned short port)
{
	//创建监听套接字
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd == -1)
	{
		perror("socket err");
		return -1;
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	//端口复用
	int opt = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	//绑定
	int ret = bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
	if (ret == -1)
	{
		perror("bind err");
		return -1;
	}

	//监听
	ret = listen(lfd, 128);
	if (ret == -1)
	{
		perror("listen err");
		return -1;
	}

	return lfd;
}

int epollRun(unsigned short port)
{
	int lfd = initListenFd(port);
	if (lfd == -1)
	{
		return -1;
	}

	//创建epoll模型
	int epfd=epoll_create(10);
	if (epfd == -1) {
		perror("epoll_create err");
		return -1;
	}

	//初始化epoll模型
	struct epoll_event ev;
	ev.data.fd = lfd;
	ev.events = EPOLLIN;
	//添加lfd到epoll模型
	int ret=epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);
	if (ret == -1) {
		perror("epoll_ctl err");
		return -1;
	}

	//循环检测
	struct epoll_event evs[1024];
	int size = sizeof(evs) / sizeof(evs[0]);
	int flag = 0;
	while (1)
	{
		if (flag) {
			break;
		}
		int num=epoll_wait(epfd, evs, size, -1);
		for (int i = 0; i < num; i++)
		{
			int curfd = evs[i].data.fd;
			if (curfd == lfd)
			{
				//连接
				int ret=acceptConn(lfd, epfd);
				//建立连接失败，终止程序
				if (ret == -1) {
					flag = 1;
					break;
				}
			}
			else 
			{
				//通信
				int ret = recvHttpRequest(curfd, epfd);
				if (ret == -1)
				{
					flag = 1;
				}
			}
		}
	}
	return 0;
}

int acceptConn(int lfd,int epfd)
{
	//建立新连接
	int cfd=accept(lfd,NULL,NULL);
	if (cfd == -1)
	{
		perror("accept err");
		return -1;
	}

	//设置文件描述符为非阻塞
	int flag = fcntl(cfd, F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(cfd, F_SETFL, flag);

	//通信文件描述符添加到epoll模型中
	struct epoll_event ev;
	ev.data.fd = cfd;
	ev.events = EPOLLIN | EPOLLET;//边沿模式
	int ret=epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
	if (ret == -1)
	{
		perror("epoll_ctl err");
		return -1;
	}
	
	return 0;
}

int recvHttpRequest(int cfd,int epfd)
{
	char tmp[1024];
	char buf[4096];

	int len, total = 0;
	//循环读数据，非阻塞cfd
	while ((len=recv(cfd,tmp,sizeof(tmp),0))>0)
	{
		if (total + len < sizeof(buf))
		{
			memcpy(buf + total, tmp, len);
		}
		total += len;
	}

	if (len == -1)
	{
		if (errno == EAGAIN) {
			//所有数据已读完
			//获取请求行中的数据

			//遍历字符串，遇到第一个\r\n->请求行拿到了
			char* pt = strstr(buf, "\r\n");
			//获取请求行长度
			int reqlen = pt - buf;
			//截断字符串，只保留请求行
			buf[reqlen] = '\0';
			//解析请求行
			parseRequestLine(buf, cfd);
		}
		else {
			perror("read err");
			return -1;
		}
	}
	else if (len == 0)
	{
		printf("client 断开连接\n");
		//服务器和客户端断开连接,cfd从epoll模型中删除
		disConnect(cfd, epfd);
	}

	return 0;
}

int parseRequestLine(const char* reqLine,int cfd)
{
	//拆分请求行 前两部分： 提交数据方式、请求文件目录
	char method[6];
	char path[1024];
	sscanf(reqLine, "%[^ ] %[^ ]", method, path);

	char* file=NULL;
	if (strcmp(path, "/") == 0)
	{
		file = "./";
	}
	else
	{
		file = path + 1;//   /hello/a 等价于 ./hello/a
	}

	printf("Requested file: %s\n", file);  // 打印请求的文件路径以调试
	decodeMsg(path, path);//中文解码

	//判断请求方式是不是get
	//http不区分大小写 get/GET/Get
	if (strcasecmp(method, "get") != 0 )
	{
		printf("用户提交的不是get请求，忽略\n");
		return -1;
	}
	//判断访问是文件还是目录 通过stat函数得到文件属性
	struct stat st;
	int ret = stat(file, &st);
	if (ret == -1)
	{
		//获取文件属性失败，没有这个文件
		//给客户端发送404
		sendHeadMsg(cfd,404,"not found",getFileType(".html"), -1);
		sendFile(cfd,"404.html");
		return -1;
	}

	if (S_ISDIR(st.st_mode))
	{
		//请求目录->遍历目录，发送目录给client
		sendHeadMsg(cfd,200,"OK",getFileType(".html"),-1);
		sendDir(cfd,file);
	}
	else
	{
		//请求文件->发送文件内容给client
		sendHeadMsg(cfd,200,"OK",getFileType(file),st.st_size);
		sendFile(cfd,file);
	}
}

int sendFile(int cfd,const char* fileName)
{
	//读文件并响应客户端
	//传输层使用tcp协议，数据块内容可以分批次发送
	int fd = open(fileName, O_RDONLY);
	while (1) {
		char buf[1024];
		int len = read(fd, buf, sizeof(buf));
		if (len > 0)
		{
			send(cfd, buf, len, 0);
			//发送端发送太快会导致接收端显示异常
			usleep(50);
		}
		else if(len==0)
		{
			break;
		}
		else{
			return -1;
		}
	}
}

//status 状态码 /status对应状态码的描述 /type：Content-Type /length：Content-Length
int sendHeadMsg(int cfd,int status,const char* descr,const char* type,int length)
{
	//状态行 消息报头 空行
	//消息报头只需要 Content_Type Content_Length
	char buf[1024];
	//拼接
	sprintf(buf, "HTTP/1.1 %d %s\r\n", status, descr);
	sprintf(buf + strlen(buf), "Content-Type: %s\r\n", type);
	sprintf(buf + strlen(buf), "Content-Length: %d\r\n\r\n", length);

	printf("head: %s\n", buf);//调试 打印请求行和请求头

	send(cfd, buf, strlen(buf), 0);
}

//服务器遍历当前目录所有文件名，发送给客户端(以表格table格式)
int sendDir(int cfd,const char* dirName)
{
	char buf[1024];
	sprintf(buf, "<html><head><title>%s</title></head><body><table>", dirName);
	struct dirent** namelist;
	int num = scandir(dirName, &namelist, NULL, alphasort);
	for (int i = 0; i < num; i++)
	{
		//取出文件名
		char* name = namelist[i]->d_name;
		struct stat st;
		//stat()第一个参数为文件的路径
		char subpath[1024];
		sprintf(subpath, "%s%s", dirName, name);
		int ret = stat(subpath, &st);
		if (S_ISDIR(st.st_mode)) {
			sprintf(buf + strlen(buf), "<tr><td><a href=\"%s/\"> % s</a>< / td><td> % ld< / td>< / tr>", name, name, (long)st.st_size);
		}
		else {
			sprintf(buf + strlen(buf), "<tr><td><a href=\"%s\"> % s</a>< / td><td> % ld< / td>< / tr>", name, name, (long)st.st_size);
		}
		send(cfd, buf, strlen(buf), 0);
		memset(buf, 0, sizeof(buf));
		//释放资源 namelist[i]这个指针指向一块有效内存
		free(namelist[i]);
	}
	//补充html中剩余的标签
	sprintf(buf, "</table></body></html>");
	send(cfd, buf, strlen(buf), 0);
	//释放namelist
	free(namelist);

	return 0;
}

int disConnect(int cfd,int epfd)
{
	int ret=epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
	close(cfd);
	if (ret == -1) {
		perror("epoll_ctl err");
		return -1;
	}
	return 0;
}

const char* getFileType(const char* filename) {
	struct FileType {
		const char* extension;
		const char* contentType;
	};

	// 定义一个包含常见文件类型的数组
	struct FileType fileTypes[] = {
		{".html", "text/html"},
		{".htm", "text/html"},
		{".css", "text/css"},
		{".js", "application/javascript"},
		{".json", "application/json"},
		{".xml", "application/xml"},
		{".jpg", "image/jpeg"},
		{".jpeg", "image/jpeg"},
		{".png", "image/png"},
		{".gif", "image/gif"},
		{".txt", "text/plain"},
		{".pdf", "application/pdf"},
		{".zip", "application/zip"},
		{".mp3", "audio/mpeg"},
		{".mp4", "video/mp4"},
		// 添加更多的文件类型
	};

	// 获取文件类型数组的大小
	const int fileTypesCount = sizeof(fileTypes) / sizeof(fileTypes[0]);
	// 找到最后一个点字符
	const char* ext = strrchr(filename, '.');
	if (ext != NULL) {
		// 遍历文件类型数组，查找匹配的后缀名
		for (int i = 0; i < fileTypesCount; i++) {
			if (strcmp(ext, fileTypes[i].extension) == 0) {
				return fileTypes[i].contentType;
			}
		}
	}
	// 如果未找到匹配的后缀名，返回默认的Content-Type
	return "application/octet-stream";
}

int hexit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a'+10;
	if (c >= 'A' && c <= 'F')
		return c - 'A'+10;

	return 0;
}

// 解码
void decodeMsg(char* to, char* from)
{
	for (; *from != '\0'; ++to, ++from)
	{
		//isxdigit->判断字符是不是16进制
		if (from[0] == '%' && isxdigit(from[1] && isxdigit(from[2])))
		{
			//将16进制数转换为十进制
			*to = hexit(from[1]) * 16 + hexit(from[2]);

			from += 2;
		}
		else
		{
			*to = *from;
		}
	}
	*to = '\0';
}