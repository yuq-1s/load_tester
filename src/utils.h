#pragma once

#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>

struct test_info {
  int concurrency;
  struct sockaddr* server;
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

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
      perror("ERROR opening socket\n");
      exit(1);
  }
  set_fl(sockfd, O_NONBLOCK);
  // Non blocking connect, for poll later
  connect(sockfd, (struct sockaddr *)info->server, sizeof(*info->server));
  return sockfd;
}
