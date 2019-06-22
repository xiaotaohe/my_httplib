/*************************************************************************
  > File Name: http0.cpp
  > Author: 陶超
  > Mail: taochao1997@qq.com 
  > Created Time: Thu 20 Jun 2019 01:14:13 AM PDT
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
using namespace std;

#define BUF_MAX 1024
#define METHOD 10

void handler_get(int sock,char* path);
void handler_post(int sock,char* path);

void go_exe(char* path,char html[]){{{
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
}/*}}}*/
//1.解析http请求报文,按行获取
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

//清楚报头
void clear_header(int sock)
{
  char line[BUF_MAX];
  do{
    get_line(sock,line,1023);
  }while(strcmp(line,"\n"));
}
//2.获取首行
int get_first(int sock,char line[],int num)
{
  return get_line(sock,line,num);
}

//3.分析http的请求方法
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

//4.构造返回结果的函数
void* handler(void* arg)
{
  cout<<"handler"<<endl;
  char buf[BUF_MAX];
  //1.先获取首行进行分析
  int sock = *((int*)arg);
  int num = get_first(sock,buf,1023);
  char* method = new char[METHOD],*query_string = new char[BUF_MAX],*path = new char[BUF_MAX];
  parse_method(buf,&method,&query_string,&path,num);
  cout<<path<<endl;
  if(strcmp(method,"GET") == 0)
  {
    cout<<"get_func"<<endl;
    handler_get(sock,path);
  }
  else if(strcmp(method,"POST") == 0)
  {
    handler_post(sock,path);
  }
}

//5.对每种情况的错误处理
void echo_error(int sock,int error)
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
}

//处理本地要回显给浏览器的文件
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
  close(fd);
}
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
    show_404(sock);
    return;
  }
  else 
  {
    //1.判断文件文件的可读性
    if(!(st.st_mode & S_IROTH))
      return;
    if(S_ISDIR(st.st_mode)){
      strcat(path,"/index.html");
      stat(path,&st);
    }
    //判断是否为可执行程序,约定可执行程序后缀为.exe
    char* exe = path+(strlen(path)-4);
    cout<<"exe: "<<exe<<endl;
    if(strcmp(exe,".exe") == 0)
    {
      cout<<"this is exe file"<<endl;
      char html[BUF_MAX];
      go_exe(path,html);//执行
      show_file(sock,html);
goto end;
    }
      
  }
  cout<<"show_file"<<endl;
  show_file(sock,path);//发送文件
  cout<<"path:\t"<<path<<endl;
end:
  {}
}

void handler_post(int sock,char* path)
{

}

int main()
{
  //1.构建服务器
  Server server("0.0.0.0",8080);
  cout<<"处于监听状态。。。。"<<endl;
  while(1)
  {
    int client_sock = -1;
    server.Accept(&client_sock);
    cout<<"get a link!"<<endl;
    char buf[BUF_MAX];

    /*
    //2.获取请求首行
    int ret = get_first(client_sock,buf,1023);
    cout<<buf;

    //3.分析首行
    char* method = new char [10];
    char* query_string = new char[BUF_MAX];
    parse_method(buf,&method,&query_string,NULL,ret);
    cout<<"method: "<<method<<endl;
    cout<<"query_string: "<<query_string<<endl;
    */

    //4.根据不同的请求返回不同的响应
    handler((void*)&client_sock);
    close(client_sock);
    
  }
  //构造response
  /*
     const char* hello = "<h1>hello wrold</h1>";
     sprintf(buf,"HTTP/1.0 200 OK\r\nContent-Length:%lu\r\n\r\n%s",strlen(hello),hello);
     write(client_sock,buf,strlen(buf));
     */
}
