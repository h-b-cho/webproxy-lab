/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

int main(void) {
    char *buf, *p;
    char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
    int n1=0, n2=0;

    //[FLAG] 11.10 숙제 - answer 01.
    /* Extract the two arguments */
    if ((buf = getenv("QUERY_STRING")) != NULL) { // getenv() : <stdlib.h> 현재 환경에 지정된 varname의 값을 포함하여 포인터를 스트링으로 리턴한다. getenv()가 스트링을 찾을 수 없는 경우, NULL이 리턴되며 errno가 설정되어 오류를 표시한다.
        // strchr() : 문자열에서 특정 문자가 1개 있는지 검사한다.
        p = strchr(buf, '&'); // 인자로 넘어온 string "num1=3&num2=5"에 대해, 문자열 내에 &의 위치를 주소로 저장한다.
        *p = '\0';            // 해당 주소의 문자 "&" 대신 NULL로 그 값을 바꾼다. 그렇게 될 시 C언어는 문자열의 끝을 NULL로 인지하므로 변수 buf에 담겨있던 문자열이 변수 buf와 변수 p에 잘려 나뉘어 담기게 된다.
        strcpy(arg1, buf+5);  // 변수 arg1에 잉여 문자열 "num1=", 총 길이 5만큼의 앞부분을 날린 변수 buf의 나머지 문자열 즉 우리가 원하는 문자를 담긴다.
        strcpy(arg2, p+6);    // 변수 arg2에 잉여 문자열 "&num2=", 총 길이 6만큼의 앞부분을 날린 변수 p의 나머지 문자열 즉 우리가 원하는 문자를 담긴다.
        n1 = atoi(arg1);      // atoi() : string을 int 형으로 변환한다.
        n2 = atoi(arg2);
    }

    //[FLAG] 11.10 숙제 - answer 02.
    /* Extract the two arguments */
    if ((buf = getenv("QUERY_STRING")) != NULL) { 
        p = strchr(buf, '&');
        *p = '\0';
        sscanf(buf, "num1=%d", &n1); // 문자열에서 형식에 맞게 데이터를 읽어와서 변수에 저장한다. scanf()가 키보드 입력을 받는 반면 sscanf()는 문자열을 입력으로 받는다.
        sscanf(p+1, "num2=%d", &n2);
    }

    /* Make the response body */
    sprintf(content, "Welcome to add.com: ");
    sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
    sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1+n2);
    sprintf(content, "%sThanks for visiting!\r\n", content);

    /* Generate the HTTP response */
    printf("Connection: close\r\n");
    printf("Content-length: %d\r\n", (int)strlen(content));
    printf("Content-type: text/html\r\n\r\n");
    printf("%s", content);
    fflush(stdout);

    exit(0);
}
/* $end adder */