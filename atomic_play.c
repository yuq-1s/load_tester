#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

_Atomic int counter = 0;
int race_counter = 0;

void* foo(void* times) {
  for (int i = 0; i < *(int*) times; ++i) {
    counter++;
    race_counter++;
  }
  return NULL;
}

int main() {
  pthread_t tid1, tid2;
  void* tret;
  int err;
  int times = 100000;
  if ((err = pthread_create(&tid1, NULL, foo, &times)) != 0)
    exit(1);
  if ((err = pthread_create(&tid2, NULL, foo, &times)) != 0)
    exit(1);
  printf("Before join: counter = %d, race_counter = %d\n", counter, race_counter);
  if ((err = pthread_join(tid1, &tret)) != 0)
    exit(1);
  if ((err = pthread_join(tid2, &tret)) != 0)
    exit(1);
  printf("After join: counter = %d, race_counter = %d\n", counter, race_counter);
}
