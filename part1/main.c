#include "uthread.h"
#include <stdio.h>

void thread_func1() {
    for (int i = 0; i < 5; ++i) {
        printf("thread 1 - i = %d\n", i);
        for (volatile int j = 0; j < 100000000; ++j); // burn time
    }
}

void thread_func2() {
    for (int i = 0; i < 5; ++i) {
        printf("thread 2 - i = %d\n", i);
        for (volatile int j = 0; j < 100000000; ++j);
    }
}

int main() {
    if (uthread_system_init(100000) < 0) {
        fprintf(stderr, "failed to init uthread system\n");
        return 1;
    }

    uthread_create(thread_func1);
    uthread_create(thread_func2);

    // keep main thread alive forever
    while (1);
}
