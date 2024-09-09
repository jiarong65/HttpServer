#include<stdio.h>
#include"server.h"

int main(int argc,char* argv[])
{
	if (argc < 3)
	{
		printf("./a.out port path\n");
		exit(0);
	}
	//切换到资源根目录
	chdir(argv[2]);
	//获取端口
	unsigned short port = atoi(argv[1]);

	//启动服务器 —> epoll
	epollRun(port);

	return 0;
}