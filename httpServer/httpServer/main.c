#include<stdio.h>
#include"server.h"

int main(int argc,char* argv[])
{
	if (argc < 3)
	{
		printf("./a.out port path\n");
		exit(0);
	}
	//�л�����Դ��Ŀ¼
	chdir(argv[2]);
	//��ȡ�˿�
	unsigned short port = atoi(argv[1]);

	//���������� ��> epoll
	epollRun(port);

	return 0;
}