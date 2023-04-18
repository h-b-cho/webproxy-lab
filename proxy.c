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

int get_request(int fd, rio_t *rio, char *method, char *uri, char *version, char *headers, char *endserver);
int request_to_server(char *method, char *uri, char *version, char *headers, char *endserver, int *clientfd);
void send_response(int connfd, int clientfd);
void read_request(rio_t *rio, char *method, char *uri, char *version, char *headers, char *endserver);
void make_headers(char *headers);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd, *clientfd;
  char hostname[MAXLINE], port[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], headers[MAXLINE], endserver[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  rio_t rio;
  strcpy(uri, "");

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from host: (%s, %s)\n", hostname, port);

    Rio_readinitb(&rio, connfd);

    if (get_request(connfd, &rio, method, uri, version, headers, endserver) < 0) // 굳이 version을 받아올 의미가 있나? 어차피 다 1.0으로 보낼건데?
    {
      Close(connfd);
    };
    make_headers(headers);
    request_to_server(method, uri, version, headers, endserver, &clientfd); // 응답 못 받았을 때의 처리 필요?
    send_response(connfd, clientfd);
    Close(connfd);
  }
  return 0;
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
  strcpy(uri, "");
  strcpy(buf, "");
  while (strcmp(buf, "\r\n"))
  {
    if (is_request_line)
    {
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
    {
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
  char new_header[MAXLINE];
  strcpy(new_header, "");
  strncpy(new_header, headers, strlen(headers) - 2);

  if (!strstr(headers, "User-Agent:"))
  {
    strcat(new_header, user_agent_hdr);
  }
  if (!strstr(headers, "Connection:"))
  {
    strcat(new_header, "Connection: close\n");
  }
  if (!strstr(headers, "Proxy-Connection:"))
  {
    strcat(new_header, "Proxy-Connection: close\n");
  }
  strcat(new_header, "\r\n");
  strcpy(headers, "");
  strcpy(headers, new_header);
}

int request_to_server(char *method, char *uri, char *version, char *headers, char *endserver, int *clientfd)
{
  char *is_port, *rest_uri;
  char request_uri[MAXLINE], full_http_request[MAXLINE], request_port[MAXLINE];

  is_port = strstr(endserver, ":");
  if (!is_port)
  {
    strcpy(request_uri, endserver);
    strcpy(request_port, "80");
  }
  else
  {
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

void send_response(int connfd, int clientfd)
{
  char buf[MAXLINE], header[MAXLINE];
  rio_t rio;
  int content_length;

  Rio_readinitb(&rio, clientfd);
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
      content_length = atoi(temp_str);
    }
    strcat(header, buf);
  }
  Rio_writen(connfd, header, strlen(header));

  char *body = malloc(content_length);
  Rio_readnb(&rio, body, content_length);
  Rio_writen(connfd, body, content_length);

  free(body);
  Close(clientfd);
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