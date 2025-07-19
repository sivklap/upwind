#include "uthread.h"
#include "scheduler.h"
#include <stdio.h>
#include <unistd.h>

void thread_func1() {
    printf("[T1] started\n");

    printf("[T1] sleeping for 1 quantum\n");
    uthread_sleep_quantums(1);

    printf("[T1] woke up and running again\n");

    printf("[T1] exiting\n");
    uthread_exit(get_current_tid());
}

void thread_func2() {
    printf("[T2] started\n");

    printf("[T2] blocking self\n");
    uthread_block(get_current_tid());

    printf("[T2] resumed after unblock\n");

    printf("[T2] exiting\n");
    uthread_exit(get_current_tid());
}

void thread_func3() {
    printf("[T3] started\n");
    for (int i = 0; i < 2; ++i) {
        printf("[T3] loop %d\n", i);
        for (volatile int j = 0; j < 100000000; ++j); // burn time
    }
    printf("[T3] exiting\n");

    uthread_exit(get_current_tid()); 
}

int main() {
    if (uthread_system_init(100000) < 0) {
        fprintf(stderr, "init failed\n");
        return 1;
    }

    printf("main: creating threads\n");
    int tid1 = uthread_create(thread_func1);
    int tid2 = uthread_create(thread_func2);
    int tid3 = uthread_create(thread_func3);

    printf("main: created tid1=%d, tid2=%d, tid3=%d\n", tid1, tid2, tid3);

    // Let timer take over — no manual schedule
    // Give time for T2 to block itself
    for (volatile int i = 0; i < 300000000; ++i);

    printf("main: unblocking thread 2\n");
    uthread_unblock(tid2);

    // Let all threads run to completion
    for (volatile int i = 0; i < 800000000; ++i);

    printf("main: exiting (done)\n");
    return 0;
}
