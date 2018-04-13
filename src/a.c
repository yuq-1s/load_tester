#include <assert.h>
#include <unistd.h>
#include <sys/poll.h>
#include <pthread.h>
#include "utils.h"

#define TIMEOUT 500
#define BUFFER_SIZE 1<<10
#define LIFE 10

long total_connection_num;
_Atomic long failed_connection_num;
_Atomic long success_connection_num;
long connection_left;

pthread_mutex_t connection_left_mutex;
pthread_barrier_t init_barrier;

const char HEADER[] = "GET / HTTP/1.0\r\n\r\n";
enum Status { READING, WRITING };

void http_test(struct pollfd* pollfds, int* lives,
    enum Status* status, const struct test_info* info);

void* http_test_init(void* info_) {
  const struct test_info* info = (const struct test_info*)info_;
  assert(info->concurrency > 0);
  printf("Thread %lu is responsible for %d concurrencies\n", pthread_self(),
      info->concurrency);
  struct pollfd* pollfds = (struct pollfd*)malloc(
      info->concurrency*sizeof(struct pollfd));
  int* lives = (int*)malloc(info->concurrency*sizeof(int));
  enum Status* status = (enum Status*)malloc(
      info->concurrency*sizeof(enum Status));
  for (int i = 0; i != info->concurrency; ++i) {
    pthread_mutex_lock(&connection_left_mutex);
    connection_left--;
    pthread_mutex_unlock(&connection_left_mutex);
    lives[i] = LIFE;
    pollfds[i].fd = get_socket(info);
    pollfds[i].events = POLLOUT;
    status[i] = WRITING;
  }
  printf("Thread %lu is waiting for barrier\n", pthread_self());
  // wait for all concurrent connections from other threads established
  pthread_barrier_wait(&init_barrier);
  printf("Thread %lu initialization succeed\n", pthread_self());
  http_test(pollfds, lives, status, info);
  free(pollfds);
  free(lives);
  free(status);
  fprintf(stdout, "Thread %lu finished testing\n", pthread_self());
  return NULL;
}

void http_test(struct pollfd* pollfds, int* lives, enum Status* status,
    const struct test_info* info) {
  int remain_concurrency = info->concurrency;
  int read_err;
  char* buffer[BUFFER_SIZE];

  while (remain_concurrency > 0) {
    int poll_err = poll(pollfds, remain_concurrency, TIMEOUT);
    if (poll_err < 0) {
      perror("poll error");
      exit(1);
    } else if (poll_err == 0) {
      for (int i = 0; i < remain_concurrency; ++i) {
        printf("timeout of thread %lu connection %d\n", pthread_self(), i);
        lives[i]--;
      }
    } else {
      for (int i = 0; i < remain_concurrency; ++i) {
        assert(lives[i] >= 0);
        if (lives[i] == 0) {
          failed_connection_num++;

new_connection:
          close(pollfds[i].fd);
          pthread_mutex_lock(&connection_left_mutex);
          if (connection_left > 0) {
            connection_left--;
            pthread_mutex_unlock(&connection_left_mutex);
            lives[i] = LIFE;
            pollfds[i].fd = get_socket(info);
            pollfds[i].events = POLLOUT;
            status[i] = WRITING;
          } else { // connection_left == 0; // 
            pthread_mutex_unlock(&connection_left_mutex);
            for (int j = i; j < remain_concurrency-1; ++j) {
              lives[j] = lives[j+1];
              pollfds[j] = pollfds[j+1];
              status[j] = status[j+1];
            }
            remain_concurrency--;
          }

        } else { // lives[i] > 0
          if (status[i] == WRITING && pollfds[i].revents & POLLOUT) {
            if ((write(pollfds[i].fd, HEADER, sizeof(HEADER))) == -1) {
              lives[i]--;
            } else {
              status[i] = READING;
              pollfds[i].events = POLLIN;
              // TODO: goto poll
            }
          } else if (status[i] == READING && pollfds[i].revents & POLLIN) {
            while ((read_err = read(pollfds[i].fd, buffer, sizeof(buffer))) > 0);
            if (read_err == -1) {
              // FIXME: code duplication
              lives[i]--;
            } else {
              // printf("read success: read_err == 0\n");
              assert(read_err == 0);
              success_connection_num++;
              goto new_connection;
            }
          } else {/* not polled to this */}
        } // lives[i] > 0
      } // foreach i
    } // poll > 0
  } // while remain > 0
}

int main(int argc, char* argv[]) {
  if (argc != 6) {
      fprintf(stderr,"usage %s <hostname> "
          "<port> <num_concurrency> <total_connection_num> "
          "<num_thread>\n", argv[0]);
      exit(1);
  }
  connection_left = atoi(argv[4]);
  int total_concurrency = atoi(argv[3]);
  if (total_concurrency <= 0 ||
      connection_left <= 0 ||
      total_concurrency > connection_left) {
    perror("Cannot use concurrency level greater than total number of requests");
    exit(1);
  }
  int num_thread = atoi(argv[5]);
  if (num_thread <= 0) {
    perror("Thread number must be greater than 0\n");
    exit(1);
  }
  pthread_barrier_init(&init_barrier, NULL, num_thread);

  const int step = total_concurrency / num_thread;
  pthread_t* thread_ids = (pthread_t*)malloc(num_thread*sizeof(pthread_t));
  const struct test_info info1 = {
      .hostname = argv[1],
      .portno = atoi(argv[2]),
      .concurrency = step
    };
  const struct test_info info2 = {
      .hostname = argv[1],
      .portno = atoi(argv[2]),
      .concurrency = total_concurrency % num_thread + step
    };
  for (int i = 0; i < num_thread-1; ++i) {
    if ((pthread_create(thread_ids+i, NULL,
            http_test_init, (void*)(&info1))) != 0) {
        perror("create thread error\n");
        exit(1);
    }
  }
  if ((pthread_create(thread_ids+num_thread-1, NULL,
          http_test_init, (void*)(&info2))) != 0) {
      perror("create thread error\n");
      exit(1);
  }

  pthread_barrier_destroy(&init_barrier);
  for (int i = 0; i < num_thread; ++i) {
    pthread_join(thread_ids[i], NULL);
  }
  printf("success: %ld, fail: %ld\n", success_connection_num, failed_connection_num);

  free(thread_ids);
  return 0;
}
