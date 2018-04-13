#pragma once

#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

#define h_addr h_addr_list[0]

struct test_info {
  const int concurrency;
  const char* hostname;
  const int portno;
};

void set_fl(int fd, int flags) {
    int val;

    if ( ( val = fcntl(fd, F_GETFL, 0) ) < 0 ) {
        perror("fcntl F_GETFL error");
        exit(1);
    }

    val |= flags;

    if (fcntl(fd, F_SETFL, val) < 0) {
        perror("fcntl F_SETFL error");
        exit(1);
    }
}

int get_socket(const struct test_info* info) {
  int sockfd;
  struct sockaddr_in serv_addr;
  struct hostent *server;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
      perror("ERROR opening socket\n");
      exit(1);
  }
  // FIXME: move gethostbyname out of the loop
  server = gethostbyname(info->hostname);
  if (server == NULL) {
      perror("ERROR, no such host\n");
      exit(1);
  }
  memset((char *) &serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  memcpy((char *)server->h_addr, 
        (char *)&serv_addr.sin_addr.s_addr,
        server->h_length);
  serv_addr.sin_port = htons(info->portno);
  set_fl(sockfd, O_NONBLOCK);
  // Non blocking connect, for poll later
  connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  return sockfd;
}
