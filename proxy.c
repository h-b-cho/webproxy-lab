#include <stdio.h>
#include <string.h>
#include "./tiny/csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define IS_LOCAL_SERVER 1

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void *thread(void *vargp);
int doit(int connfd);
int get_request(int fd, rio_t *rio, char *method, char *uri, char *version, char *headers, char *endserver);
int request_to_server(char *method, char *uri, char *version, char *headers, char *endserver, int *clientfd);
void send_response(int connfdp, int clientfd);
void read_request(rio_t *rio, char *method, char *uri, char *version, char *headers, char *endserver);
void make_headers(char *headers);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
  int listenfd;
  int *connfdp;
  // char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // 전달 받은 포트 번호를 사용해 listen 소켓 생성.

  while (1)
  {
    clientlen = sizeof(clientaddr);

    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 클라이언트 연결 요청 수신

    // Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    // printf("Accepted connection from host: (%s, %s)\n", hostname, port);

    Pthread_create(&tid, NULL, thread, connfdp);
  }
  return 0;
}

int doit(int connfdp)
{
    int *clientfd;
    rio_t rio;
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE], headers[MAXLINE], endserver[MAXLINE];
    strcpy(uri, ""); // 초기화!

    Rio_readinitb(&rio, connfdp); // 구조체 rio에 connfd을 할당하겠다는 초기화.

    if (get_request(connfdp, &rio, method, uri, version, headers, endserver) < 0) // 굳이 version을 받아올 의미가 있나? 어차피 다 1.0으로 보낼건데?
    {// 에러 처리. get_request()에서 -1이 리턴된 경우 connfd를 최종적으로 닫아준 뒤 모든 수행을 종료한다.
      Close(connfdp);
    };

    make_headers(headers);
    request_to_server(method, uri, version, headers, endserver, &clientfd); // 응답 못 받았을 때의 처리 필요?
    send_response(connfdp, clientfd);

    return 0;
}

void *thread(void *vargp) // 
{
    int connfd = *((int *)vargp); // void pointer는 주소값만 담겨 있고 연산이나 참조를 할 수 없기 때문에 int pointer로 우선 캐스팅했다.
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

int get_request(int fd, rio_t *rio, char *method, char *uri, char *version, char *headers, char *endserver)
{
  read_request(rio, method, uri, version, headers, endserver);

  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) // RFC 1945 - GET/HEAD/POST + 토큰의 extension-method..?
  {
    clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
    return -1;
  }

  if (!strcmp(version, "HTTP/1.1"))
  {
    strcpy(version, "HTTP/1.0");
  }
  if (!strstr(headers, "Host:"))
  {
    clienterror(fd, headers, "400", "Bad Request",
                "Host header is empty in the request");
    return -1;
  }
  return 0;
}

void read_request(rio_t *rio, char *method, char *uri, char *version, char *headers, char *endserver)
{
  char buf[MAXLINE];
  int is_request_line;
  char *host_idx, *host_name_s, *path;
  is_request_line = 1;
  // 초기화!
  strcpy(uri, "");
  strcpy(buf, "");

  while (strcmp(buf, "\r\n"))
  {
    if (is_request_line)
    {
      // if문 안쪽은 method, uri, version에 대한 정보가 담겨 있는 첫번째 줄을 읽어오는 수행을 한다.
      Rio_readlineb(rio, buf, MAXLINE);
      sscanf(buf, "%s %s %s", method, uri, version);
      host_name_s = strstr(uri, "http://");
      path = host_name_s ? strstr(host_name_s + 7, "/") : strstr(uri, "/");
      if (path)
      {
        strcpy(uri, path);
      }
      else
      {
        strcpy(uri, "/");
      }
      is_request_line = 0;
    }
    else
    { // 나머지 요청의 내용을 읽는다. 헤더 정보 중 변경해줘야 하는 설정들이 있다면 수정해준다.
      Rio_readlineb(rio, buf, MAXLINE);
      if (strstr(buf, "Connection:"))
      {
        strcpy(buf, "Connection: close\n");
      }
      if (strstr(buf, "Proxy-Connection:"))
      {
        strcpy(buf, "Proxy-Connection: close\n");
      }
      if (host_idx = strstr(buf, "Host: "))
      {
        strncpy(endserver, host_idx + 6, strlen(host_idx) - 7);
        endserver[strlen(endserver) - 1] = '\0';
      }
      strcat(headers, buf);
    }
  }
}

void make_headers(char *headers)
{
  char tmp[MAXLINE];
  strcpy(tmp, ""); // 빈 변수 tmp를 선언한다.
  strncpy(tmp, headers, strlen(headers) - 2); // read_request()에서 받아온 headers의 내용 전체에서 맨 마지막의 \r\n을 제외하여 빈 변수 tmp에 담는다.

  // 필수 헤더 정보 미포함 시 내용을 추가해준다.
  if (!strstr(headers, "User-Agent:"))
  {
    strcat(tmp, user_agent_hdr);
  }
  if (!strstr(headers, "Connection:"))
  {
    strcat(tmp, "Connection: close\n");
  }
  if (!strstr(headers, "Proxy-Connection:"))
  {
    strcat(tmp, "Proxy-Connection: close\n");
  }

  strcat(tmp, "\r\n"); // tmp의 끝에 \r\n을 붙여준다.
  strcpy(headers, ""); // free().
  strcpy(headers, tmp); // 변수 headers에 갱신된 헤더의 내용을 도로 담는다.
}

int request_to_server(char *method, char *uri, char *version, char *headers, char *endserver, int *clientfd)
{
  char *is_port, *rest_uri;
  char request_uri[MAXLINE], full_http_request[MAXLINE], request_port[MAXLINE];

  is_port = strstr(endserver, ":"); // 요청의 내용에 서버의 포트 번호가 있었다면 is_port == 0. 포트 번호에 대한 정보의 유무를 콜론의 유무로 확인하고 있다.

  if (!is_port)
  {// NULL이면, 즉 요청값 중 포트번호에 대한 정보가 없었다면.
    strcpy(request_uri, endserver);
    strcpy(request_port, "80");
  }
  else
  {// NULL이 아니면, 즉 요청값 중 포트번호에 대한 정보가 있었다면.
    strncpy(request_uri, endserver, (int)(is_port - endserver));
    strcpy(request_port, is_port + 1);
  }

  sprintf(full_http_request, "%s %s %s\n%s\r\n\r\n", method, uri, version, headers);

  if (IS_LOCAL_SERVER)
  {
    *clientfd = Open_clientfd("localhost", request_port);
  }
  else
  {
    *clientfd = Open_clientfd(request_uri, request_port);
  }

  Rio_writen(*clientfd, full_http_request, strlen(full_http_request));
  return 0;
};

void send_response(int connfdp, int clientfd)
{
  char buf[MAXLINE], header[MAXLINE];
  rio_t rio;
  int content_length;

  Rio_readinitb(&rio, clientfd); // 구조체 rio에 clientfd을 할당하겠다는 초기화.

  // 초기화!
  strcpy(buf, "");
  strcpy(header, "");

  while (strcmp(buf, "\r\n"))
  {
    Rio_readlineb(&rio, buf, MAXLINE);
    if (strstr(buf, "Content-length:"))
    {
      char *p = strchr(buf, ':');
      char temp_str[MAXLINE];
      strcpy(temp_str, p + 1);
      content_length = atoi(temp_str); // header안에 들어 있는 Content-length 정보를 따로 변수 content_length에 담아 추출한다.
    }
    strcat(header, buf);
  }

  Rio_writen(connfdp, header, strlen(header));

  char *body = malloc(content_length);

  Rio_readnb(&rio, body, content_length);
  Rio_writen(connfdp, body, content_length);

  free(body);
  
  // Close(clientfd);
}

void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}