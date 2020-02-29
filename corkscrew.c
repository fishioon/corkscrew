#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
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
  char *host = NULL, *desthost = NULL, *destport = NULL, *up = NULL;
  int port, csock, infd, outfd, kq, nev, i, evfd;
  struct timespec timeout;
  struct kevent kev[2], events[2];
  ssize_t len;

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

  infd = STDIN_FILENO;
  outfd = STDOUT_FILENO;
  timeout.tv_sec = 5;
  timeout.tv_nsec = 0;
  kq = kqueue();
  EV_SET(&kev[0], csock, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, &csock);
  EV_SET(&kev[1], infd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, &infd);
  for (;;) {
    nev = kevent(kq, kev, 2, events, 2, &timeout);
    for (i = 0; i < nev; ++i) {
      evfd = *((int *)events[i].udata);
      if (evfd == csock) {
        len = read(csock, buffer, sizeof(buffer));
        if (len <= 0)
          break;
        len = write(outfd, buffer, len);
        if (len <= 0)
          break;
      }
      if (evfd == infd) {
        len = read(infd, buffer, sizeof(buffer));
        if (len <= 0)
          break;
        len = write(csock, buffer, len);
        if (len <= 0)
          break;
      }
    }
  }
  exit(0);
}
