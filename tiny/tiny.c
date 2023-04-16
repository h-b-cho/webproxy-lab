/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

// sprintf(): 지정된 문자열 버퍼에 서식화된 문자열을 써넣는다.
// fprintf(): 파일(또는 다른 출력 스트림)에 서식화된 문자열을 써넣는다. 파일 뿐만 아니라 다른 출력 스트림(예: 표준 출력, 표준 에러 등)에도 쓸 수 있다.

void doit(int fd);
void read_requesthdrs(rio_t *rp); // Rio == Robust I/O (input/output) functions. "Robust"는 "강건한" 또는 "견고한"을 의미하는 형용사다. 따라서 "Robust I/O functions"은 안전하고 견고한 I/O 함수를 의미한다.
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, int is_head_method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, int is_head_method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

// 입력이 ./tiny 8000 일 때, argc==2, argv[0]=="tiny", argv[1]==8000 이다. 
// 리눅스 시스템 프로그래밍에서 int main의 매개변수는 argc – 명령행 인자 개수, argv – 명령행 인자 벡터 (문자열의 배열), envp – 환경 변수 목록.
int main(int argc, char **argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command-line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* Open_listenfd 함수를 호출해서 듣기 소켓을 오픈한다. 인자로 포트번호를 넘겨준다. */
    // Open_listenfd는 요청받을 준비가 된 듣기 식별자를 리턴한다. <-- listenfd
    listenfd = Open_listenfd(argv[1]);

    /* 전형적인 무한 서버 루프를 실행 */
    while (1) {
		// accept 함수 인자에 넣기 위한 주소 길이를 계산한다.
		clientlen = sizeof(clientaddr);

		/* 반복적으로 연결 요청을 접수 */
		// accept 함수는 1. 듣기 식별자, 2. 소켓주소구조체의 주소, 3. 주소(소켓구조체)의 길이를 인자로 받는다.
		connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);

		// Getaddrinfo는 호스트 이름: 호스트 주소, 서비스 이름: 포트 번호의 스트링 표시를 소켓 주소 구조체로 변환.
		// Getnameinfo는 위를 반대로 소켓 주소 구조체에서 스트링 표시로 변환.
		Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		printf("Accepted connection from (%s, %s)\n", hostname, port);

		/* 트랜젝션을 수행 */
		doit(connfd);

		/* 트랜잭션이 수행된 후 자신 쪽의 연결 끝 (소켓) 을 닫는다. */
		Close(connfd); // 자신 쪽의 연결 끝을 닫는다.
        printf("===============================================\n\n");
    }
}

	void doit(int fd) {
		int is_static;
		struct stat sbuf;
		char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
		char filename[MAXLINE], cgiargs[MAXLINE];
  		int is_head_method; //[FLAG] 11.11 숙제

		rio_t rio;

		/* Read request line and headers */
		/* rio_t 구조체에 대한 초기화 */
		Rio_readinitb(&rio, fd); // &rio 주소를 가지는 읽기 버퍼와 식별자 connfd를 연결한다.
		Rio_readlineb(&rio, buf, MAXLINE); // 버퍼에서 읽은 것이 담겨있다.
		printf("Request headers:\n");
		printf("%s", buf); //[FLAG] 11.11 숙제 - telnet 연결 후 입력: "GET / HTTP/1.1" 혹은 "HEAD / HTTP/1.1". 입력 후 엔터 두 번.
		sscanf(buf, "%s %s %s", method, uri, version);

		if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) { //[FLAG] 11.11 숙제
			clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
			return;
		}

		//[FLAG] 11.11 숙제 - http HEAD method
		if (strcasecmp(method, "HEAD") == 0){
			is_head_method = 1;
		} else {
			is_head_method = 0;
		}
		
		/* GET method라면 읽어들이고, 다른 요청 헤더들을 무시하고 있다. */
		read_requesthdrs(&rio);
		
		/* Parse URI form GET request */
		/* URI 를 파일 이름과 비어 있을 수도 있는 CGI 인자 스트링으로 분석하고, 요청이 정적 또는 동적 컨텐츠를 위한 것인지 나타내는 플래그를 설정한다. */
		is_static = parse_uri(uri, filename, cgiargs);
		printf("uri : %s, filename : %s, cgiargs : %s \n", uri, filename, cgiargs);

		/* 만일 파일이 디스크상에 있지 않으면, 에러메세지를 즉시 클라아언트에게 보내고 메인 루틴으로 리턴 */
		if (stat(filename, &sbuf) < 0) { //stat는 파일 정보를 불러오고 sbuf에 내용을 적어준다. ok 0, errer -1
			clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
			return;
		}

		if (is_static) { /* Serve static content */
			// 파일 읽기 권한이 있는지 확인하기
			// S_ISREG : 일반 파일인가? , S_IRUSR: 읽기 권한이 있는지? S_IXUSR 실행권한이 있는가?
			if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
				// 권한이 없다면 클라이언트에게 에러를 전달
				clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
				return;
			}
			// 그렇다면 클라이언트에게 파일 제공
			serve_static(fd, filename, sbuf.st_size, is_head_method);
		} else { /* Serve dynamic content */
			/* 실행 가능한 파일인지 검증 */
			if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
				// 실행이 불가능하다면 에러를 전달
				clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
				return;
			}
			// 그렇다면 클라이언트에게 파일 제공.
			serve_dynamic(fd, filename, cgiargs, is_head_method);
		}
	}

	void read_requesthdrs(rio_t *rp) {
		char buf[MAXLINE];

		Rio_readlineb(rp, buf, MAXLINE);
		
		/* strcmp는 두 문자열을 비교하는 함수다. 헤더의 마지막 줄은 비어 있기에 \r\n 만 buf에 담겨 있다면 while문을 탈출한다. */
		while (strcmp(buf, "\r\n"))
		{
			//rio_readlineb는 \n를 만날 때 멈춘다.
			Rio_readlineb(rp, buf, MAXLINE);
			printf("%s", buf);
			// 멈춘 지점 까지 출력하고 다시 while.
		}
		return;
	}

	int parse_uri(char *uri, char *filename, char *cgiargs) {
		char *ptr;
		
		/* strstr은 문자열 안에서 문자열로 검색하는 함수다. strstr으로 cgi-bin이 들어있는지 확인하고 양수값을 리턴하면 dynamic content를 요구하는 것이기에 조건문을 탈출. */
		if (!strstr(uri, "cgi-bin")) { 
			/* Static content*/
			strcpy(cgiargs, "");
			strcpy(filename, ".");
			strcat(filename, uri);

			//결과 cgiargs = "" 공백 문자열, filename = "./~~ or ./home.html
			// uri 문자열 끝이 / 일 경우 home.html을 filename에 붙혀준다.
			if (uri[strlen(uri) - 1] == '/') {
				strcat(filename, "home.html");
			}
			else if (strcmp(uri, "/gif") == 0) {
				strcpy(filename, "godzilla.gif");
			}
			else if (strcmp(uri, "/mp4") == 0) {
				strcpy(filename, "moodo.mp4");
			}
			return 1;
		} else { 
			/* Dynamic content*/
			ptr = index(uri, '?');
			// index 함수는 문자열에서 특정 문자의 위치를 반환한다
			// CGI인자 추출
			if (ptr) {
			// 물음표 뒤에 있는 인자 다 갖다 붙인다.
			// 인자로 주어진 값들을 cgiargs 변수에 넣는다.
			strcpy(cgiargs, ptr + 1);
			// 포인터는 문자열 마지막으로 바꾼다.
			*ptr = '\0'; // uri물음표 뒤 다 없애기
			} else {
			strcpy(cgiargs, ""); // 물음표 뒤 인자들 전부 넣기
			}
			strcpy(filename, "."); // 나머지 부분 상대 uri로 바꿈,
			strcat(filename, uri); // ./uri 가 된다.
			return 0;
		}
	}

	void serve_static(int fd, char *filename, int filesize, int is_head_method) {
		int srcfd;
		char *srcp, filetype[MAXLINE], buf[MAXBUF];

		if (is_head_method == 1) {
			/* Send response headers to client */
			get_filetype(filename, filetype);
			sprintf(buf, "HTTP/1.0 200 OK\r\n");
			sprintf(buf, "%sServer : Tiny Web Server \r\n", buf);
			sprintf(buf, "%sConnection : close \r\n", buf);
			sprintf(buf, "%sConnect-length : %d \r\n", buf, filesize);
			sprintf(buf, "%sContent-type : %s \r\n\r\n", buf, filetype);
			Rio_writen(fd, buf, strlen(buf));
			printf("Response headers : \n");
			printf("%s", buf);
		} else {
			/* Send response headers to client */
			get_filetype(filename, filetype);
			sprintf(buf, "HTTP/1.0 200 OK\r\n");
			sprintf(buf, "%sServer : Tiny Web Server \r\n", buf);
			sprintf(buf, "%sConnection : close \r\n", buf);
			sprintf(buf, "%sConnect-length : %d \r\n", buf, filesize);
			sprintf(buf, "%sContent-type : %s \r\n\r\n", buf, filetype);
			Rio_writen(fd, buf, strlen(buf));
			printf("Response headers : \n");
			printf("%s", buf);

			/* Send reponse body to client */
			srcfd = Open(filename, O_RDONLY, 0);
			// srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //[FLAG] 11.9 숙제 - mmap 대체
			srcp = (char *)malloc(filesize);
			Rio_readn(srcfd, srcp, filesize); //srcfd, not filename. filesize 만큼의 가상 메모리(힙)를 할당한 후(malloc은 아무 것도 없는 빈 상태에서 시작) , Rio_readn()으로 할당된 가상 메모리 공간의 시작점인 fbuf를 기준으로 srcfd 파일을 읽어 복사해넣는다.
			Close(srcfd);
			Rio_writen(fd, srcp, filesize); // Rio_writen() 함수(시스템 콜)를 통해 클라이언트에게 전송한다.
			// Munmap(srcp, filesize); // Mmap은 Munmap, malloc은 free로 할당된 가상 메모리를 해제해준다.
			free(srcp);
		}
	}

	void get_filetype(char *filename, char *filetype) {
		if (strstr(filename, ".html")) {
			strcpy(filetype, "text/html");
		} else if (strstr(filename, ".gif")) {
			strcpy(filetype, "image/gif");
		} else if (strstr(filename, ".png")) {
			strcpy(filename, "image/.png");
		} else if (strstr(filename, ".jpg")) {
			strcpy(filetype, "image/jpeg");
		} else if (strstr(filename, ".mp4")) { //[FLAG] 11.7 숙제 - MP4 비디오 파일 처리
			strcpy(filetype, "video/mp4");
		} else {
			strcpy(filetype, "text/plain");
		}
	}

	void serve_dynamic(int fd, char *filename, char *cgiargs, int is_head_method) {
		char buf[MAXLINE], *emptylist[] = {NULL};

		/* Return first part of HTTP respone */
		sprintf(buf, "HTTP/1.0 200 OK\r\n");
		Rio_writen(fd, buf, strlen(buf));
		sprintf(buf, "Server: Tiny Web Server\r\n");
		Rio_writen(fd, buf, strlen(buf));

		if (Fork() == 0) {
			/* Child */
			/* Real server would set all CGI vars here */
			setenv("QUERY_STRING", cgiargs, 1);
			Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */
			Execve(filename, emptylist, environ); /* Run CGI program */
		}
		Wait(NULL); /* Parent waits for and reaps child */
	}

  	void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
		char buf[MAXLINE], body[MAXBUF];

		/* Bulid the HTTP response body */
		sprintf(body, "<html><title>Tiny Error</title>");
		sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
		sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
		sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
		sprintf(body, "%s<hr><em>The Tiny Web server</em\r\n", body);

		/* Print the HTTP response */
		sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
		Rio_writen(fd, buf, strlen(buf));
		sprintf(buf, "Content-type: text/html\r\n");
		Rio_writen(fd, buf, strlen(buf));
		sprintf(buf, "Content-length: %d\r\n\r\n", (int) strlen(body));
		Rio_writen(fd, buf, strlen(buf));
		Rio_writen(fd, body, strlen(body));
	}