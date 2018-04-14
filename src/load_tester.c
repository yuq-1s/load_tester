#include <assert.h>
#include <unistd.h>
#include <sys/poll.h>
#include <pthread.h>
#include "utils.h"

#define TIMEOUT 500
#define BUFFER_SIZE 1<<10
#define LIFE 20

long total_connection_num;
_Atomic long failed_connection_num;
_Atomic long success_connection_num;
long connection_left;
_Atomic long http_response_200;
_Atomic long http_response_500;
_Atomic long http_response_other;

pthread_mutex_t connection_left_mutex;
pthread_barrier_t init_barrier;

const char HEADER[] = "GET / HTTP/1.0\r\n\r\n";
const char GOOD_HEADER[] = "HTTP/1.1 200 OK";
const char ERROR_HEADER[] = "HTTP/1.1 500 OK";
static_assert(sizeof(GOOD_HEADER) == sizeof(ERROR_HEADER), "HEADER length not match");

enum Status { READING_STATUS_CODE, READING, WRITING };

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
  char buffer[BUFFER_SIZE];
  char state_code_buffer[sizeof(GOOD_HEADER)];

  while (remain_concurrency > 0) {
    int poll_err = poll(pollfds, remain_concurrency, TIMEOUT);
    if (poll_err < 0) {
      perror("poll error");
      exit(1);
    } else {
      // FIXME: Is erase OK?
      for (int i = 0; i < remain_concurrency; ++i) {
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
        } else if (poll_err == 0) { // and lives[i] > 0
          /*printf("timeout of thread %lu connection %d\n", pthread_self(), i);*/
          lives[i]--;
        } else {
          assert(lives[i] > 0);
          assert(poll_err > 0);
          if (status[i] == WRITING && pollfds[i].revents & POLLOUT) {
            if ((send(pollfds[i].fd, HEADER, sizeof(HEADER), MSG_NOSIGNAL)) == -1) {
              lives[i]--;
            } else {
              status[i] = READING_STATUS_CODE;
              pollfds[i].events = POLLIN;
            }
          } else if (status[i] == READING_STATUS_CODE &&
              pollfds[i].revents & POLLIN) {
            // printf("%.*s\n", sizeof(GOOD_HEADER), buffer);
            if ((read_err = read(pollfds[i].fd, state_code_buffer,
                    sizeof(state_code_buffer))) == -1) {
              lives[i]--;
            } else if (read_err == (int)sizeof(GOOD_HEADER)) {
              if (strncmp(state_code_buffer, GOOD_HEADER, sizeof(GOOD_HEADER)-1) == 0) {
                http_response_200++;
                status[i] = READING;
              } else if (read_err == (int)sizeof(GOOD_HEADER)) {
                http_response_500++;
                status[i] = READING;
              } else {
                http_response_other++;
                status[i] = READING;
              }
            } else { // read less than sizeof(GOOD_HEADER) bytes, bad network!
              http_response_other++;
              status[i] = READING;
            }
          } else if (status[i] == READING && pollfds[i].revents & POLLIN) {
            while ((read_err = read(pollfds[i].fd, buffer, sizeof(buffer))) > 0);
            if (read_err == -1) {
              lives[i]--;
            } else {
              assert(read_err == 0);
              success_connection_num++;
              goto new_connection;
            }
          } else {/* nothing polled to this */}
        } // poll > 0 && lives[i] > 0
      } // foreach i
    } // poll >= 0
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
  printf("success: %ld, fail: %ld\n",
      success_connection_num,
      failed_connection_num);
  printf("200 OK: %ld, 500 Internal Error: %ld, other response: %ld\n",
      http_response_200,
      http_response_500,
      http_response_other);

  free(thread_ids);
  return 0;
}
