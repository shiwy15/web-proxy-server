/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"
#include "stdio.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread(void *vargs); // 스레드 함수 원형 선언

/* 🚨 메인 함수 🚨 --------------------------------------- */
// argc는 옵션의 개수, arvg는 옵션 문자열의 배열
// (char **argv) = *argv[]
// (argv[0] : 실행파일 이름, argv[1] : 포트번호)
int main(int argc, char **argv) { 
  int listenfd, connfd;  // client, server 소켓 fd
  char hostname[MAXLINE], port[MAXLINE]; // 클라이언트의 호스트명과 포트번호 저장
  socklen_t clientlen; // 클라이언트의 소켓 구조체 크기를 저장
  struct sockaddr_storage clientaddr; // 클라이언트의 소켓 주소를 저장
  pthread_t tid;  // 각 스레드를 식별할 변수

  /* 1. command line 명령 체크 */
  // 만약 명령행의 변수가 2개가 아니라면(포트번호를 제대로 입력x), 에러 메세지 출력 & 프로그램 종료
  if (argc != 2) { 
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  /* 2. 지정된 포트를 바인딩하고 대기중인 소켓을 생성 */
    // open_listenfd()를 호출, 반환된 소켓 파일 디스크립터를 listenfd에 저장
  listenfd = Open_listenfd(argv[1]);
  
  /* 3. 무한루프 시작 */
  while (1) {
    /* 4. 주소 길이 계산 (accept 함수 인자에 넣음) */
    clientlen = sizeof(clientaddr);
    /* 5. accept 함수를 호출하여 클라이언트의 연결 요청을 기다림(반복) */
      // 서버 소켓(listenfd)에서 클라이언트의 연결 요청이 발생할 때까지 블로킹
      // 연결 요청이 발생하면 클라이언트와 통신을 위한 새로운 소켓(connfd)을 생성
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  
    /* 6. 소켓주소 구조체에서 해당 소켓에 대한 클라이언트의 ip주소와 포트번호를 문자열로 변환하여 출력 */
    // 클라이언트의 주소 정보가 담긴 clientaddr 구조체에서 호스트이름과 포트번호를 추출하여 hostname과 port 변수에 저장
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, (void *)connfd);  // 스레드 만들기
    /* 7. http 트랜잭션 처리 및 연결 종료 */
    // doit(connfd);  // http 트랜잭션 처리
    // Close(connfd);  // 클라이언트와 연결 종료, 소켓 파일 디스크립터(sfd)를 닫음.
  }
}

/* 🔥 클라이언트 요청을 처리하기 위한 스레드 함수 */
void *thread(void *vargs) {
  int connfd = (int)vargs;
  Pthread_detach(pthread_self());
  // 분리형 함수(detachable) : 각 스레드가 독립적으로, 서로의 결과를 기다리지 않으므로 성능향상에 도움
  // 해당 스레드가 실행 종료 후 자동으로 메모리에서 제거 될 수 있도록 함.
  doit(connfd);
  close(connfd);
}

/* 🚨 doit 함수 🚨 ---------------------------------------*/
/* 클라이언트로부터의 http 요청을 처리하는 함수
   클라이언트로부터의 요청은 rio 버퍼를 통해 읽어들이며, buf 변수에 저장됨.
   buf 변수의 첫 번째 라인은 http요청 라인으로, 이를 공백 문자를 기준으로 파싱하여 method, uri, version 변수에 저장
   이후 read_requesthdrs 함수를 호출하여 추가적인 요청 헤더들을 읽어들임
   parse_uri 함수를 호출하여 uri를 파싱하고, 요청된 파일이 정적 파일인지, 동적 파일인지 판별함.
   파일이 존재하지 않을 땐 404에러를 반환하고, 정적파일일 땐 serve_static을 호출하여 해당파일을 클라이언트에게 전송
   동적 파일인 경우 serve_dynamic을 호출하여 해당 cgi 프로그램을 실행하고, 결과를 클라이언트에게 전송*/
void doit(int fd) {  // 매개변수로 클라이언트와 연결된 파일디스크립터(fd)를 가짐
  int is_static;  // 정적파일인지 아닌지를 판단해주는 변수
  struct stat sbuf;  // 파일에 대한 정보를 가지고 있는 구조체
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];  
  // http 요청 메세지를 전달할 버퍼와 요청라인을 파싱하여 저장할 변수들
  char filename[MAXLINE], cgiargs[MAXLINE];  // 요청된 파일의 경로 저장 변수, 동적콘텐츠를 실행하기 위한 cgi인자를 저장하는 문자열 변수
  rio_t rio;  // 클라이언트와 통신을 위한 rio 구조체. 클라이언트와의 연결을 유지하며, 요청을 읽어오는 버퍼 역할 수행

  /* 1. 클라이언트의 요청과 헤더를 읽어옴 : 요청 라인을 읽고 구문을 분석, 이를 위해 rio_readlineb 함수를 사용 */
  Rio_readinitb(&rio, fd);  // 클라이언트와 통신을 위한 rio(robust i/o) 구조체 초기화
  Rio_readlineb(&rio, buf, MAXLINE);  // http 요청 메세지 읽어옴. (buf 에서 client request 읽어들이기)
  printf("Request headers:\n");  // 읽은 http 요청 메세지 출력
  printf("%s", buf);  
  
  /* 2. 읽어들인 요청 헤더에서 클라이언트의 요청 http메서드, uri, http버전을 파싱 */
  sscanf(buf, "%s %s %s", method, uri, version);  // sscanf() : 버퍼에서 포맷을 지정하여 읽어오는 함수

  /* 3. http가 get 메서드가 아닐 경우 501 에러 반환 : tiny는 get 방식만 지원 */
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  /* 4. 클라이언트로부터 추가적인 요청 헤더를 읽어들임 */
  // Q) 왜 여기서는 fd변수를 사용하지 않는가?
  // rio 구조체를 사용하여 클라이언트가 전송한 데이터를 읽어들이고 버퍼에 저장함.
  // 아래 함수에서는 이 버퍼에서 요청 헤더를 추출하여 처리함. 그래서 fd 필요 없음.
  read_requesthdrs(&rio);

  /* 5. uri를 파싱하고, 요청된 파일이 정적, 동적(cgi)인지 판별 */
  // stat(filename, &sbuf) : filename에 대한 정보를 sbuf 구조체에 저장. 조회에 실패 시 -1을 반환함
  is_static = parse_uri(uri, filename, cgiargs);
  if (stat(filename, &sbuf) < 0) {  // filename에 맞는 정보를 조회하지 못했으면(-1) 에러 메세지 출력
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }
  /* 6. 정적 파일인 경우 : 파일이 존재하는지, 읽기 권한이 있는지 확인하고 클라이언트로 전송*/
  if (is_static) {
    // sbuf 구조체에 저장된 파일의 모드 정보(st_mode)를 활용하여 파일이 정규파일인지, 사용자 읽기 권한이 있는지를 검사
    // POSIX 표준 매크로 --> S_ISREG: 일반 파일, S_IRUSR: 읽기 권한, S_IXUSR: 실행 권한
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method); // 응답
  }

  /* 7. 동적 파일인 경우 : 실행 권한이 있는지 확인하고 실행 시킨 뒤, 결과를 클라이언트로 전송 */
  else {  // 파일이 정규파일이 아니거나 실행할 수 없으면 에러메세지 출력 
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);  // 응답
  }
}

/* 🚨 clienterror 함수 🚨 ---------------------------------------*/
// 에러 발생 시, client에게 보내기 위한 응답(에러메세지)
// 매개변수: 클라이언트와 연결된 파일 디스크립터, 오류가 발생한 이유(cause), http 오류 코드, 오류메세지, 오류 상세 메세지
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* http 응답 body 문자열 생성(html 형식의 오류 페이지를 만들기 위함) */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s, %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* http 응답 헤더 생성 */
  // buf 문자열을 만들어 응답 헤더를 생성하고, rio_writen 함수를 사용하여 fd에 응답을 작성함.
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/* 🚨 read_requesthdrs 함수 🚨 ---------------------------------------*/
// request header를 읽기 위한 함수
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  // strcmp : 두 문자열을 비교하는 함수
  // 헤더의 마지막 줄은 비어있으므로, \r\n만 buf에 담겨있으면 while문 탈출
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);  // \n을 만날 때 멈춤
    printf("%s", buf);  // 멈춘 지점까지 출력하고 다시 while
  }
  return;
}

/* 🚨 parse_uri 함수 🚨 ---------------------------------------*/
// uri 파싱 함수. 정적 요청이면 0, 동적 요청이면 1을 반환
// strstr(대상 문자열, 검색할 문자열) : 문자열 안에서 문자열로 검색하는 기능, 문자열 시작 포인터를 반환(없으면 null 반환)
// strcpy(대상 문자열, 원본 문자열) : 문자열 복사하기, 대상 문자열의 시작 포인터 반환
// strcat(최종 문자열, 붙일 문자열) : 문자열 서로 붙이기. 최종 문자열의 시작 포인터 반환. 
int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  /* 1. 파싱 결과 정적 요청인 경우 : uri에 cgi-bin이 포함X */ 
  if (!strstr(uri, "cgi-bin")) { 
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    // 요청에서 특정 정적 콘텐츠를 요구하지 않은 경우, homepage 반환
    if (uri[strlen(uri)-1] == '/')  // uri 문자열 끝이 '/'일 경우,
      strcat(filename, "home.html");  // home.html을 filename 끝에 붙여줌
    return 1;
  }
  /* 2. 파싱 결과 동적 요청인 경우 */ 
  else {
    // uri부분에서 fileneme과 args를 구분하는 ? 위치 찾기
    // index 함수는 문자열에서 특정 문자의 위치 반환
    ptr = index(uri, '?'); 
    if (ptr) {  // ?가 있으면
      strcpy(cgiargs, ptr+1);  // cgiargs에 인자 넣어주기
      *ptr = '\0'; 
    }
    else  // ?가 없으면
      strcpy(cgiargs, "");
    // filename에 uri 담기
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

/* 🚨 serve_static 함수 🚨 ---------------------------------------*/
// 정적 파일을 처리하여 http응답을 클라이언트에게 전송
// 먼저 요청된 파일의 확장자를 통해 MIME타입을 결정하고, http응답 헤더를 생성
// 그 다음 파일을 열고 읽기 전용으로 매핑, 매핑된 메모리를 사용하여 응답 본문을 클라이언트에게 전송하고 메모리해제
// 마지막엔 클라이언트와 연결이 끊기고 함수가 종료됨
void serve_static(int fd, char *filename, int filesize, char *method) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  get_filetype(filename, filetype);  // 요청된 파일의 확장자를 통해 mime 타입 결정
  sprintf(buf, "HTTP/1.0 200 OK\r\n");  // http 응답 상태 코드, 버전
  // http 응답 헤더 추가 
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);  // 서버 이름
  sprintf(buf, "%sConnection: close\r\n", buf);  // 연결 방식
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);  // 파일 크기
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);  // mime 방식
  Rio_writen(fd, buf, strlen(buf));  // 응답 헤더를 클라이언트에게 전송하는 함수
  printf("Response headers:\n");
  printf("%s", buf);

  if(!strcasecmp(method, "HEAD")) {
    return;
  }

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);  // 파일 열기
  srcp = (char *)malloc(filesize); // 파일 크기만큼 메모리 할당(연습문제 11.9)
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);  // 파일 매핑
  // mmap 함수를 사용하여 srcfd가 가리키는 파일의 내용을 메모리에 매핑하고, 매핑된 내용에 대한 포인터(srcp)를 반환
  // 0은 메모리 주소를 자동으로 선택하도록 지시
  // prot_read : 매핑된 메모리를 읽기 전용으로 지정
  // map_private : 매핑된 메모리를 프로세스 전용으로 지정
  Rio_readn(srcfd, srcp, filesize); 
  // 주어진 파일 디스크립터(srcfd)부터 최대 filesize만큼 데이터를 읽어서 srcp에 저장
  Close(srcfd);  // 파일 닫기
  Rio_writen(fd, srcp, filesize);  // 응답 본문을 클라이언트에 전송
  free(srcp); // 할당 해제(연습문제 11.9)
  // Munmap(srcp, filesize);  // 매핑된 메모리 해제
}

/* 🚨 get_filetype 함수 🚨 ---------------------------------------*/
// html 서버가 처리할 수 있는 파일 타입을 이 함수를 통해 제공
void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html"); 
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4")) // 11.7 연습문제
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

/* 🚨 serve_dynamic 함수 🚨 ---------------------------------------*/
// cgiargs : cgi프로그램에 전달될 인자
void serve_dynamic(int fd, char *filename, char *cgiargs) {
char buf[MAXLINE], *emptylist[] = { NULL };

/* http응답의 첫 부분을 구성 
  : 문자열 버퍼를 이용하여 http/1.0 200 ok와 server:~~~을 각각 생성하고,
  rio_writen 함수를 사용하여 클라이언트에게 전송 */
sprintf(buf, "HTTP/1.0 200 OK\r\n");
Rio_writen(fd, buf, strlen(buf));
sprintf(buf, "Server: Tiny Web Server\r\n");
Rio_writen(fd, buf, strlen(buf));

/* fork 함수를 사용하여 자식 프로세스를 생성*/
// fork : 새로운 프로세스를 생성하는 시스템 콜. 호출한 프로세스의 복제본을 만들어 새로운 프로세스를 생성
if (Fork() == 0) { 
  /* Real server would set all CGI vars here */
  setenv("QUERY_STRING", cgiargs, 1); 
  //setenv 함수로 query_string 환경변수를 cgiargs로 설정
  Dup2(fd, STDOUT_FILENO); // dup2 함수로 표준 출력을 클라이언트로 리디렉션
  Execve(filename, emptylist, environ); 
  // filename으로 지정된 cgi 프로그램 실행. 
  // emptylist는 cgi 프로그램에 전달할 인자가 없음을 나타내는 빈 배열
  // environ은 현재 환경변수를 나타내는 전역 변수
}
Wait(NULL); 
// 부모프로세스는 wait 함수를 사용하여 자식 프로세스가 종료될 때까지 기다림.
// 자식 프로세스가 종료되면 반환값을 무시하고 함수를 종료함.
}