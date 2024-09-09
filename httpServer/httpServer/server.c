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
	//���������׽���
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
	//�˿ڸ���
	int opt = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	//��
	int ret = bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
	if (ret == -1)
	{
		perror("bind err");
		return -1;
	}

	//����
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

	//����epollģ��
	int epfd=epoll_create(10);
	if (epfd == -1) {
		perror("epoll_create err");
		return -1;
	}

	//��ʼ��epollģ��
	struct epoll_event ev;
	ev.data.fd = lfd;
	ev.events = EPOLLIN;
	//���lfd��epollģ��
	int ret=epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);
	if (ret == -1) {
		perror("epoll_ctl err");
		return -1;
	}

	//ѭ�����
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
				//����
				int ret=acceptConn(lfd, epfd);
				//��������ʧ�ܣ���ֹ����
				if (ret == -1) {
					flag = 1;
					break;
				}
			}
			else 
			{
				//ͨ��
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
	//����������
	int cfd=accept(lfd,NULL,NULL);
	if (cfd == -1)
	{
		perror("accept err");
		return -1;
	}

	//�����ļ�������Ϊ������
	int flag = fcntl(cfd, F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(cfd, F_SETFL, flag);

	//ͨ���ļ���������ӵ�epollģ����
	struct epoll_event ev;
	ev.data.fd = cfd;
	ev.events = EPOLLIN | EPOLLET;//����ģʽ
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
	//ѭ�������ݣ�������cfd
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
			//���������Ѷ���
			//��ȡ�������е�����

			//�����ַ�����������һ��\r\n->�������õ���
			char* pt = strstr(buf, "\r\n");
			//��ȡ�����г���
			int reqlen = pt - buf;
			//�ض��ַ�����ֻ����������
			buf[reqlen] = '\0';
			//����������
			parseRequestLine(buf, cfd);
		}
		else {
			perror("read err");
			return -1;
		}
	}
	else if (len == 0)
	{
		printf("client �Ͽ�����\n");
		//�������Ϳͻ��˶Ͽ�����,cfd��epollģ����ɾ��
		disConnect(cfd, epfd);
	}

	return 0;
}

int parseRequestLine(const char* reqLine,int cfd)
{
	//��������� ǰ�����֣� �ύ���ݷ�ʽ�������ļ�Ŀ¼
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
		file = path + 1;//   /hello/a �ȼ��� ./hello/a
	}

	printf("Requested file: %s\n", file);  // ��ӡ������ļ�·���Ե���
	decodeMsg(path, path);//���Ľ���

	//�ж�����ʽ�ǲ���get
	//http�����ִ�Сд get/GET/Get
	if (strcasecmp(method, "get") != 0 )
	{
		printf("�û��ύ�Ĳ���get���󣬺���\n");
		return -1;
	}
	//�жϷ������ļ�����Ŀ¼ ͨ��stat�����õ��ļ�����
	struct stat st;
	int ret = stat(file, &st);
	if (ret == -1)
	{
		//��ȡ�ļ�����ʧ�ܣ�û������ļ�
		//���ͻ��˷���404
		sendHeadMsg(cfd,404,"not found",getFileType(".html"), -1);
		sendFile(cfd,"404.html");
		return -1;
	}

	if (S_ISDIR(st.st_mode))
	{
		//����Ŀ¼->����Ŀ¼������Ŀ¼��client
		sendHeadMsg(cfd,200,"OK",getFileType(".html"),-1);
		sendDir(cfd,file);
	}
	else
	{
		//�����ļ�->�����ļ����ݸ�client
		sendHeadMsg(cfd,200,"OK",getFileType(file),st.st_size);
		sendFile(cfd,file);
	}
}

int sendFile(int cfd,const char* fileName)
{
	//���ļ�����Ӧ�ͻ���
	//�����ʹ��tcpЭ�飬���ݿ����ݿ��Է����η���
	int fd = open(fileName, O_RDONLY);
	while (1) {
		char buf[1024];
		int len = read(fd, buf, sizeof(buf));
		if (len > 0)
		{
			send(cfd, buf, len, 0);
			//���Ͷ˷���̫��ᵼ�½��ն���ʾ�쳣
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

//status ״̬�� /status��Ӧ״̬������� /type��Content-Type /length��Content-Length
int sendHeadMsg(int cfd,int status,const char* descr,const char* type,int length)
{
	//״̬�� ��Ϣ��ͷ ����
	//��Ϣ��ͷֻ��Ҫ Content_Type Content_Length
	char buf[1024];
	//ƴ��
	sprintf(buf, "HTTP/1.1 %d %s\r\n", status, descr);
	sprintf(buf + strlen(buf), "Content-Type: %s\r\n", type);
	sprintf(buf + strlen(buf), "Content-Length: %d\r\n\r\n", length);

	printf("head: %s\n", buf);//���� ��ӡ�����к�����ͷ

	send(cfd, buf, strlen(buf), 0);
}

//������������ǰĿ¼�����ļ��������͸��ͻ���(�Ա��table��ʽ)
int sendDir(int cfd,const char* dirName)
{
	char buf[1024];
	sprintf(buf, "<html><head><title>%s</title></head><body><table>", dirName);
	struct dirent** namelist;
	int num = scandir(dirName, &namelist, NULL, alphasort);
	for (int i = 0; i < num; i++)
	{
		//ȡ���ļ���
		char* name = namelist[i]->d_name;
		struct stat st;
		//stat()��һ������Ϊ�ļ���·��
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
		//�ͷ���Դ namelist[i]���ָ��ָ��һ����Ч�ڴ�
		free(namelist[i]);
	}
	//����html��ʣ��ı�ǩ
	sprintf(buf, "</table></body></html>");
	send(cfd, buf, strlen(buf), 0);
	//�ͷ�namelist
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

	// ����һ�����������ļ����͵�����
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
		// ��Ӹ�����ļ�����
	};

	// ��ȡ�ļ���������Ĵ�С
	const int fileTypesCount = sizeof(fileTypes) / sizeof(fileTypes[0]);
	// �ҵ����һ�����ַ�
	const char* ext = strrchr(filename, '.');
	if (ext != NULL) {
		// �����ļ��������飬����ƥ��ĺ�׺��
		for (int i = 0; i < fileTypesCount; i++) {
			if (strcmp(ext, fileTypes[i].extension) == 0) {
				return fileTypes[i].contentType;
			}
		}
	}
	// ���δ�ҵ�ƥ��ĺ�׺��������Ĭ�ϵ�Content-Type
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

// ����
void decodeMsg(char* to, char* from)
{
	for (; *from != '\0'; ++to, ++from)
	{
		//isxdigit->�ж��ַ��ǲ���16����
		if (from[0] == '%' && isxdigit(from[1] && isxdigit(from[2])))
		{
			//��16������ת��Ϊʮ����
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