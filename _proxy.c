#include <stdio.h>
#include <string.h>
#include "./tiny/csapp.h"
//
//
//
// F A I L
//
//
//

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int produce_header(int fd, rio_t *rio, char *method, char *uri, char *version, char *headers, char *endserver);
int request_to_server(char *method, char *uri, char *version, char *headers, char *endserver, int *clientfd);
void send_response(int connfd, int clientfd);
void read_request(rio_t *rio, char *method, char *uri, char *version, char *headers, char *endserver);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
	int listenfd, connfd, *clientfd;
	char hostname[MAXLINE], port[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], headers[MAXLINE], endserver[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	rio_t from_client_rio;

	if (argc != 2)
	{
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	listenfd = Open_listenfd(argv[1]);

	while (1)
	{
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		printf("Accepted connection from host: (%s, %s)\n", hostname, port);

		Rio_readinitb(&from_client_rio, connfd); // 구조체 rio에 connfd을 할당하겠다는 초기화.

		if (produce_header(connfd, &from_client_rio, method, uri, version, headers, endserver) < 0) // 굳이 version을 받아올 의미가 있나? 어차피 다 1.0으로 보낼건데?
		{// 에러 처리. produce_header()에서 -1이 리턴된 경우 connfd를 최종적으로 닫아준 뒤 모든 수행을 종료한다.
			Close(connfd);
			continue;
		};

		request_to_server(method, uri, version, headers, endserver, &clientfd); // 응답 못 받았을 때의 처리 필요?

		send_response(connfd, clientfd);

		Close(connfd);
	}
	return 0;
}

int produce_header(int fd, rio_t *rio, char *method, char *uri, char *version, char *headers, char *endserver)
// C 언어는 반드시 하나의 값을 리턴하므로, 여러 개의 변수에 값을 할당하려는 경우에는 이런 식으로 1. 위 25행과 같이 빈 문자열들을 선언, 2. 요청값을 처리할 함수에 주소값들을 매개변수로 넘김 3. 할당 의 과정을 밟을 수 있다.
{
	read_request(rio, method, uri, version, headers, endserver);

	if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD"))
	{
		clienterror(fd, method, "501", "Not Implemented",
					"Proxy does not implement this method");
		return -1;
	}

	// strcmp() : 두 문자열을 순서대로 비교하다가 다른 문자를 처음 만났을 때 1이나 -1을 리턴한다. 완전히 일치하면 0을 리턴한다.
	if (!strcmp(version, "1.1")) {
		strcpy(version, "HTTP/1.0");
	}

	// strstr() : 일치하는 부분의 시작 위치에 해당하는 문자열의 주소를 리턴한다.
	if (!strstr(headers, "User-Agent:")) {
		strcat(headers, user_agent_hdr);
	}
	if (!strcasestr(headers, "Connection:")) {
		strcat(headers, "Connection: close");
	}
	printf("read_request 후 헤더:\n%s\n", headers);
	printf("compare result: %p\n", strcasestr(headers, "Proxy-Connection:"));
	if (!strcasestr(headers, "Proxy-Connection:")) {
		strcat(headers, "Proxy-Connection: close");
	}

	if (!strstr(headers, "Host: ")) { // WHY? strcasestr()는 안 되는 거지?
		clienterror(fd, headers, "400", "Bad Request",
					"Host header is empty in the request");
		return -1;
	}
	printf("PRODUCE_HEADER headers: \n%s", headers);
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
				strcpy(buf, "Proxy-Connection: close\n"); // 헤더 파싱 안됨?
			}
			if (host_idx = strstr(buf, "Host: "))
			{
				strncpy(endserver, host_idx + 6, strlen(host_idx) - 7);
				endserver[strlen(endserver) - 1] = '\0';
			}
			strcat(headers, buf);
		}
	}
	printf("READ_REQUEST headers: \n%s", headers);
}

int request_to_server(char *method, char *uri, char *version, char *headers, char *endserver, int *clientfd)
{
	char *is_port, *rest_uri;
	char request_uri[MAXLINE], full_http_request[MAXLINE], request_port[MAXLINE];

	is_port = strstr(endserver, ":"); // 포트번호에 대한 정보의 유무를 콜론의 유무로 확인하고 있다.

	if (!is_port) 
	{// NULL이면, 즉 요청값 중 포트번호에 대한 정보가 없었다면.
		strcpy(request_uri, endserver);
		strcpy(request_port, "80"); // default port인 80번을 열어준다.
	}
	else
	{// NULL이 아니면, 즉 요청값 중 포트번호에 대한 정보가 있었다면.
		strncpy(request_uri, endserver, (int)(is_port - endserver));
		strcpy(request_port, is_port + 1);
	}

	sprintf(full_http_request, "%s %s %s\n%s\r\n\r\n", method, uri, version, headers);

	*clientfd = Open_clientfd(request_uri, request_port);
	Rio_writen(*clientfd, full_http_request, strlen(full_http_request));

	return 0;
};

void send_response(int connfd, int clientfd)
{
	char buf[MAXLINE], header[MAXLINE];
	int CONTENT_LEN;
	rio_t to_server_rio;

	strcpy(buf, "");

	Rio_readinitb(&to_server_rio, clientfd); // 구조체 rio에 clientfd을 할당하겠다는 초기화.
	printf("CLIENTFD: %d", clientfd);

	printf("----header while start\n");
	while (1)
	{
		Rio_readlineb(&to_server_rio, buf, MAXLINE);
		printf("buffer: %s\n", buf);
		strcat(header, buf);
		if (strcmp(buf, "\r\n") == 0) {
			break;
		} // request 중 header까지를 readline으로 한 줄 한 줄 받아온다. \r\n\r\n가 나오면 header의 끝이니까.

		if (strstr(buf, "Content-length :")) {
			char* is_header = strchr(buf, ':');
			char content_len_str[MAXLINE];
			strcpy(content_len_str, is_header + 1);
			CONTENT_LEN = atoi(content_len_str);
		}// header안에 들어 있는 Content-length 정보를 따로 변수 CONTENT_LEN에 담아 추출한다.
		strcpy(buf, "");
	}
	printf("----header while end\n");

	char response[strlen(header)+CONTENT_LEN]; // response 전체의 길이는 여기서 결정된다. response 길이 == header의 길이와 header안에 들어있던 body의 길이를 합친 길이.
	char content[CONTENT_LEN];

	strcat(response, header);

	printf("----body while start\n");
	while (1)
	{
		Rio_readlineb(&to_server_rio, buf, MAXLINE);
		strcat(content, buf);
		if (strlen(buf) == 0) {
			break;
		}
		strcpy(buf, "");
	}
	printf("----body while end\n");
	strcat(response, content);
	Rio_writen(connfd, response, strlen(response));

	Close(clientfd);
};

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