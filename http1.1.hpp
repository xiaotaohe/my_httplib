/*************************************************************************
    > File Name: http0.hpp
    > Author: 陶超
    > Mail: taochao1997@qq.com 
    > Created Time: Fri 21 Jun 2019 01:30:42 AM PDT
 ************************************************************************/

#include"tcp.hpp"
#include"show_error.hpp"
#include<iostream>
#include<stdio.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/fcntl.h>
#include<sys/types.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
#include<sys/stat.h>
#include<sys/sendfile.h>
#include<string.h>
#include<pthread.h>
#include<atomic>
using namespace std;

#define BUF_MAX 1024
#define METHOD 10

void handler_get(int sock,char* path);
void handler_post(int sock,char* path);

//解析时要保存的数据
char* method = new char[METHOD],*query_string = new char[BUF_MAX],*path = new char[BUF_MAX];
void clear_tmp()
{
  delete method;
  delete query_string;
  delete path;
}

//1.处理含有可执行程序的请求
void go_exe(char* path,char html[])
{
  //1.子进程进行程序替换，将结果写入管道
  //2.父进程读去结果写入html
  //通过管道，将可执行程序的结果，写入html
  int fd[2];
  pipe(fd);
  char new_path[BUF_MAX];
  sprintf(new_path,"./%s",path);
  pid_t pid = fork();
  if(pid ==  0)
  {
    //子进程进行程序替换
    //子进程写入结果
    close(fd[0]);
    dup2(fd[1],1);
    execlp(new_path,new_path,NULL);
  }
  else 
  {
    close(fd[1]);
    wait(pid);
    int rz = read(fd[0],html,BUF_MAX);
  }
}

//2.解析http请求报文,按行获取

int get_line(int sock,char line[],int num)
{
  assert(line);
  assert(num>0);

  char c = 'A';
  int i = 0;
  while(i<num-1 && c!='\n')
  {
    int s = recv(sock,&c,1,0);
    if(s > 0)
    {
      if(c == '\r')
      {
        //窥探
        recv(sock,&c,1,MSG_PEEK);
        if(c == '\n')
          recv(sock,&c,1,0);
        else 
          c = '\n';
      }
      line[i++] = c;
    }
  }
  line[i] = '\0';
  return i;
}

//3.清除报头
void clear_header(int sock)
{
  char line[BUF_MAX];
  do{
    get_line(sock,line,1023);
  }while(strcmp(line,"\n"));
}
//4.获取首行
int get_first(int sock,char line[],int num)
{
  return get_line(sock,line,num);
}

//5.分析http的请求方法
void parse_method(char first[],char** method,char** query_string,char** path = NULL, int num = 0)
{
  int i = 0;
  char* url = NULL;
  char* version = NULL;
  int index = 0;
  *method = first;
  //1.分离出method //GET http://   ?zhangsan=nan&sex=nan HTTP/1.0 ---> GET
  while(i<num-1 && first[i] != ' ')
    i++;
  first[i++] = '\0';

  //2.分离url 
  url = first+i;
  while(i<num-1 && first[i] != ' ' &&  first[i] != '?')
    i++;

  if(first[i] == '?')
  {
    first[i++] = '\0';
    *query_string = first+i;
    while(i<num-1 &&first[i] != ' ')
      i++;
  }

  first[i++] = '\0';
  //3.分离出协议版本
  version = first+i;  
  if(path != NULL){
    *path = url;
  }

  //用于测试分析
  /*
  cout<<"url: "<<url<<endl;
  cout<<"version: "<<version<<endl;
  */ 
}

//6.请求的分流
void* handler(void* arg)
{ 
  cout<<"handler run"<<endl;
  char buf[BUF_MAX];
  //1.先获取首行进行分析
  int sock = *((int*)arg);
  cout<<"client_sock: "<<sock<<endl;
  int num = get_first(sock,buf,1023);
  parse_method(buf,&method,&query_string,&path,num);
  cout<<path<<endl;
  if(strcmp(method,"GET") == 0)
  {
    cout<<"get_func"<<endl;
    handler_get(sock,path);
    close(sock);
    return NULL;
  }
  else if(strcmp(method,"POST") == 0)
  {
    handler_post(sock,path);
    close(sock);
    return NULL;
  }
  return NULL;
}

//7.错误页处理
void echo_error(int sock,int error)/*{{{*/
{
  switch(error)
  {
    case 400:
      break;
    case 403:
      break;
    case 404:
      {
        cout<<"send over!"<<endl;
        show_404(sock);
        break;
      }
  }
}/*}}}*/

//8.处理本地要回显给浏览器的文件
void show_file(int sock,char* path)
{
  //1.先处理html文件
  struct stat st;
  bool n_is_file = false;
  if(stat(path,&st)<0)
    n_is_file = true;
  cout<<"size: "<<st.st_size<<endl;
  //2.构造html页面
  char buf[BUF_MAX];
  sprintf(buf,"HTTP/1.0 200 OK\r\n");
  send(sock,buf,strlen(buf),0);
  sprintf(buf,"Content-Type:text/html;charset=utf-8\r\n");
  send(sock,buf,strlen(buf),0);
  sprintf(buf,"\r\n");
  send(sock,buf,strlen(buf),0);

  int fd = open(path,O_RDONLY);
  //1.如果是文件
  if(!n_is_file)
    sendfile(sock,fd,NULL,st.st_size);
  //2.如果是输出结果
  else 
  {
    sprintf(buf,"<html><body style=\"font-size:80px\">%s</body></html>",path);
    send(sock,buf,strlen(buf),0);
  }
  cout<<"sendfile over!"<<endl;
  //close(sock);//所有的关闭client_sock统一由handler处理
}

//9.处理get方法
void handler_get(int sock,char* path_ptr)
{
  //1.根据需要，此时的报头就不再处理了
  clear_header(sock);
  //2.拼接路径
  char path[BUF_MAX] = {0};
  sprintf(path,"wwwroot%s",path_ptr);
  cout<<"ptah: "<<path<<endl;
  struct stat st;
  //3.判断文件/目录属性
  if(stat(path,&st)<0)//文件不存在
  {
    cout<<"file not existed!"<<endl;
    echo_error(sock,404);
  goto end;
  }
  else 
  {
    //1.判断文件文件的可读性
    if(!(st.st_mode & S_IROTH))
    {
      echo_error(sock,404);
    goto end;
    }
    if(S_ISDIR(st.st_mode)){
      struct stat st_index;
      strcat(path,"/index.html");
      if(stat(path,&st_index)<0)
      {
        echo_error(sock,404);
      goto end;
      }
    }
    //判断是否为可执行程序,约定可执行程序后缀为.exe
    char* exe = path+(strlen(path)-4);
    cout<<"exe: "<<exe<<endl;
    if(strcmp(exe,".exe") == 0)
    {
      //1.是exe文件，先判断是否存在
      struct stat st;
      if(stat(path,&st)<0)
        echo_error(sock,404);
      else 
      {
        cout<<"this is exe file"<<endl;
        char html[BUF_MAX];
        go_exe(path,html);//执行
        show_file(sock,html);
      }
    goto end;
    }    
  }
  cout<<"show_file"<<endl;
  show_file(sock,path);//发送文件
  cout<<"path:\t"<<path<<endl;
end:
  {}
}
//10.处理post方法
void handler_post(int sock,char* path)
{}


void* start(void* arg)
{
  int sock = *((int*)arg);
  handler(&sock);
  close(sock);
}



class Http_Server
{
  public:
    Http_Server(const string& ip,uint16_t port)
      :server(NULL)
    {
      server = new Server(ip,port);
      if(server == NULL)
      {
        cout<<"Server create error!";
        exit(-1);
      }
    }
    ~Http_Server()
    {
      clear_tmp();
      delete server;
    }
    void Http_Work()
    {
      //1.采用多线程模型
      cout<<"server start...."<<endl;
      int client_sock;
      while(1)
      {
        server->Accept(&client_sock);
        cout<<"get new link...."<<endl;
        //2.通过子线程来服务新连接
        pthread_t ret = 0;
        pthread_create(&ret,NULL,&start,(void*)&client_sock);
        pthread_detach(ret);
      }
    }
  private:
    Server* server;
};






