#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>

#define TIMEOUT 5000
#define LIFE 5
#define h_addr h_addr_list[0]

long total_connection_num;
_Atomic long failed_connection_num;
_Atomic long success_connection_num;
_Atomic long connection_left;

const char* host;
int portno;

const char HEADER[] = "GET / HTTP/1.0\r\n\r\n"; //User-Agent: asdf\nAccept:*/*\r\n";

/*struct fragile_socket {*/
  /*struct pollfd* pollsockfd;*/
  /*int life;*/
/*};*/

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

int get_socket(/*const char* host, const int portno*/) {
  int sockfd;
  struct sockaddr_in serv_addr;
  struct hostent *server;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
      perror("ERROR opening socket\n");
      exit(1);
  }
  // FIXME: move gethostbyname out of the loop
  server = gethostbyname(host);
  if (server == NULL) {
      perror("ERROR, no such host\n");
      exit(1);
  }
  memset((char *) &serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  memcpy((char *)server->h_addr, 
        (char *)&serv_addr.sin_addr.s_addr,
        server->h_length);
  serv_addr.sin_port = htons(portno);
  set_fl(sockfd, O_NONBLOCK);
  // Non blocking connect, for poll later
  connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  return sockfd;
  /*return {.pollsockfd = {.fd = sockfd, .events = POLLOUT}, .life = LIFE};*/
}

void* do_test(
    struct pollfd* pollfds,
    int* lives,
    const int num_concurrency/*,
    const char* host,
    const int portno*/) {
  char buffer[1<<16];
  int read_err, write_err;
  while (connection_left > 0 ||
      success_connection_num + failed_connection_num != total_connection_num) {
    int poll_err = poll(pollfds, num_concurrency, TIMEOUT);
    if (poll_err == -1) {
      perror("poll error");
      exit(1);
    } else if (poll_err > 0) {
      for (int i = 0; i != num_concurrency; ++i) {
        if (lives[i] <= 0) {
          printf("connection %ld fails\n", total_connection_num-connection_left);
          failed_connection_num++;
          close(pollfds[i].fd);
          pollfds[i].fd = get_socket();
          pollfds[i].events = POLLOUT;
          lives[i] = LIFE;
        }
        if (pollfds[i].revents & POLLOUT) {
          connection_left--;
          printf("connection left: %ld\n", connection_left);
          printf("writing the %ldth message\n", total_connection_num-connection_left);
          if ((write_err = write(pollfds[i].fd, HEADER, sizeof(HEADER))) == -1) {
            lives[i]--;
            /*failed_connection_num++;*/
            /*close(pollfds[i].fd);*/
            /*pollfds[i].fd = get_socket();*/
            /*pollfds[i].events = POLLOUT;*/
            /*lives[i] = LIFE;*/
            /*continue;*/
          } else {
            pollfds[i].events = POLLIN;
          }
        }
        if (pollfds[i].revents & POLLIN) {
          // Ignore any server content.
          printf("reading the %ldth message\n", total_connection_num-connection_left);
          while ((read_err = read(pollfds[i].fd, buffer, sizeof(buffer))) > 0);
          if (read_err == -1) {
            // FIXME: code duplication
            lives[i]--;
            /*printf("read fail with -1\n");*/
            /*failed_connection_num++;*/
            /*close(pollfds[i].fd);*/
            /*pollfds[i].fd = get_socket();*/
            /*pollfds[i].events = POLLOUT;*/
            /*lives[i] = LIFE;*/
            /*continue;*/
          } else {
            printf("read success: read_err == 0\n");
            assert(read_err == 0);
            success_connection_num++;
            close(pollfds[i].fd);
            pollfds[i].fd = get_socket();
            pollfds[i].events = POLLOUT;
            lives[i] = LIFE;
            /*continue;*/
          }
        }
      }
    } else {
      assert(poll_err == 0);
      printf("timeout, residule life: ");
      for (int i = 0; i != num_concurrency; ++i) {
        printf("%d ", lives[i]);
        lives[i]--;
        /*assert(lives[i] >= 0);*/
        /*if (lives[i] == 0) {*/
          /*failed_connection_num++;*/
          /*close(pollfds[i].fd);*/
          /*pollfds[i].fd = get_socket();*/
          /*pollfds[i].events = POLLOUT;*/
          /*lives[i] = LIFE;*/
          /*[>continue;<]*/
        /*}*/
      }
      printf("\n");
    }
  }
  return NULL;
}

void* test(const int num_concurrency/*, const char* host, const int portno*/) {
  assert(num_concurrency > 0);
  struct pollfd* pollfds = (struct pollfd*)malloc(num_concurrency*sizeof(struct
        pollfd));
  int* lives = (int*)malloc(num_concurrency*sizeof(int));
  for (int i = 0; i != num_concurrency; ++i) {
    lives[i] = LIFE;
    pollfds[i].fd = get_socket();
    pollfds[i].events = POLLOUT;
  }
  void* ret = do_test(pollfds, lives, num_concurrency/*, host, portno*/);
  free(pollfds);
  free(lives);
  return ret;
  /*struct fragile_socket* fds = (struct fragile_socket*)malloc(*/
      /*num_concurrency * sizeof(struct fragile_socket));*/
  /*for (int i = 0; i != num_concurrency; ++i) {*/
    /*fds[i] = get_socket(host, portno);*/
  /*}*/
}

int main (int argc, char* argv[])
{
  // TODO: dynamic scheduler
  if (argc != 5) {
      fprintf(stderr,"usage %s <hostname> <port> <total_connection_num> <num_concurrency>\n", argv[0]);
      exit(1);
  }
  host = argv[1];
  portno = atoi(argv[2]);
  total_connection_num = atoi(argv[3]);
  connection_left = total_connection_num;
  test(atoi(argv[4]));
  printf("success: %ld, fail: %ld\n", success_connection_num, failed_connection_num);
}
  /*sockfd = get_socket(argv[1], atoi(argv[2]));

  fds[0].fd = STDIN_FILENO;
  fds[0].events = POLLIN;

  fds[1].fd = STDOUT_FILENO;
  fds[1].events = POLLOUT;

  ret = poll(fds, 2, TIMEOUT * 1000);

  if (ret == -1) {
    perror ("poll");
    return 1;
  }

  if (!ret) {
    printf ("%d seconds elapsed.\n", TIMEOUT);
    return 0;
  }

  if (fds[0].revents & POLLIN)
    printf ("stdin (%d) is readable\n", STDIN_FILENO);

  if (fds[1].revents & POLLOUT)
    printf ("stdout (%d) is writable\n", STDOUT_FILENO);

  return 0;*/
