#include "types.h"
#include "user.h"

#define NTHREADS 5

int c = 10;

void* thread_func(void* arg) {
  int thread_id = *(int*)arg;
  
  printf(1, "Thread %d is running\n", thread_id);
  c -= 1;
  printf(1, "%d\n", c);
  sleep(10000);
  exit();
}

int main() {
  int i;
  int thread_args[NTHREADS];
  thread_t threads[NTHREADS];
  // Create threads
  for (i = 0; i < NTHREADS; i++) {
    thread_args[i] = i;
    printf(1, "for문 : %d\n", thread_args);
    int res = thread_create((uint*)&threads[i], thread_func, (void*)&thread_args[i]);
    printf(1, "%s %d %d\n", __func__, __LINE__, res);
    if (res != 0) {
      printf(1, "Failed to create thread\n");
      exit();
    }
    sleep(100);
  }
  // // Wait for threads to finish
  for (i = 0; i < NTHREADS; i++) {
    sleep(100);
    // if (thread_join(i, 0) != 0) {
    //   printf(1, "Failed to join thread\n");
    //   exit();
    // }
  }


  exit();
}