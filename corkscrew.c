#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#if HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#define BUFSIZE 4096

void usage(void) {
  printf("usage: corkscrew <proxyhost> <proxyport> <desthost> <destport> "
         "[base64auth]\n");
}

int sock_connect(const char *hname, int port) {
  int fd;
  struct sockaddr_in addr;
  struct hostent *hent;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1)
    return -1;

  hent = gethostbyname(hname);
  if (hent == NULL)
    addr.sin_addr.s_addr = inet_addr(hname);
  else
    memcpy(&addr.sin_addr, hent->h_addr, hent->h_length);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)))
    return -1;

  return fd;
}

int main(int argc, char *argv[]) {
  char uri[BUFSIZE], buffer[BUFSIZE];
  char *host = NULL, *desthost = NULL, *destport = NULL;
  char *up = NULL, line[4096];
  int port, sent, setup, code, csock;
  fd_set rfd, sfd;
  struct timeval tv;
  ssize_t len;
  FILE *fp;

  if (argc < 5) {
    usage();
    exit(-1);
  }

  host = argv[1];
  port = atoi(argv[2]);
  desthost = argv[3];
  destport = argv[4];
  up = getenv("CORKSCREW_AUTH");
  if (argc >= 6) {
    up = argv[5];
  }
  len = snprintf(uri, strlen(uri), "CONNECT %s:%s HTTP/1.1%s%s\r\n\r\n",
                 desthost, destport, up ? "\nProxy-Authorization: Basic " : "",
                 up ? up : "");

  csock = sock_connect(host, port);
  if (csock == -1) {
    fprintf(stderr, "Couldn't establish connection to proxy: %s\n",
            strerror(errno));
    exit(-1);
  }
  len = write(csock, uri, len);
  if (len <= 0) {
    exit(-1);
  }
  len = read(csock, buffer, sizeof(buffer));
  if (len <= 0) {
    exit(-1);
  }
  // sscanf(buffer, "%s%d%[^\n]", version, &code, descr);

  tv.tv_sec = 5;
  tv.tv_usec = 0;
  for (;;) {
    FD_ZERO(&sfd);
    FD_ZERO(&rfd);
    FD_SET(csock, &sfd);
    FD_SET(csock, &rfd);
    FD_SET(0, &rfd);

    if (select(csock + 1, &rfd, &sfd, NULL, &tv) == -1)
      break;

    if (FD_ISSET(csock, &rfd)) {
      len = read(csock, buffer, sizeof(buffer));
      if (len <= 0)
        break;
      len = write(1, buffer, len);
      if (len <= 0)
        break;
    }

    if (FD_ISSET(0, &rfd)) {
      len = read(0, buffer, sizeof(buffer));
      if (len <= 0)
        break;
      len = write(csock, buffer, len);
      if (len <= 0)
        break;
    }
  }
  exit(0);
}
