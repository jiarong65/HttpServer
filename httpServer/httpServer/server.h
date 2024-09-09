#ifndef SERVER_H
#define SERVER_H

//����������ҵ���߼�

//��ʼ�������ļ�������
int initListenFd(unsigned short port);
//����epollģ��
int epollRun(unsigned short port);
//����������
int acceptConn(int lfd, int epfd);
//���տͻ���http������Ϣ
int recvHttpRequest(int cfd,int epfd);

char* kmp(char* s, char* p);

//����������
int parseRequestLine(const char* reqLine,int cfd);
//��������
int sendFile(int cfd,const char* fileName);
//����ͷ���ݣ�״̬�У���Ӧͷ�����У�
int sendHeadMsg(int cfd,int status, const char* descr, const char* type, int length);
//����Ŀ¼
int sendDir(int cfd,const char* dirName);
//�Ϳͻ��˶Ͽ�����
int disConnect(int cfd,int epfd);
//��ȡ�ļ�����
const char* getFileType(const char* filename);

//����
int hexit(char c);
void decodeMsg(char* to, char* from);

#endif