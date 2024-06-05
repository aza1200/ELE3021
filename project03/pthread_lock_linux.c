#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdatomic.h>

int shared_resource = 0;

#define NUM_ITERS 100000
#define NUM_THREADS 10000

atomic_int mutex = 1;

void lock();
void unlock();

void lock() {
    int expected = 1;
    while (!atomic_compare_exchange_weak(&mutex, &expected, 0)) {
        expected = 1;
        usleep(100);  // CPU 사용을 줄이기 위해 작은 sleep을 사용할 수 있습니다.
    }
}

void unlock() {
    atomic_store(&mutex, 1);
}


void* thread_func(void* arg) {
    int tid = *(int*)arg;
    
    lock();
        for(int i = 0; i < NUM_ITERS; i++)    shared_resource++;
    unlock();
    pthread_exit(NULL);
}

int main() {
    pthread_t threads[NUM_THREADS];
    int tids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        tids[i] = i;
        pthread_create(&threads[i], NULL, thread_func, &tids[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("shared: %d\n", shared_resource);
    
    return 0;
}