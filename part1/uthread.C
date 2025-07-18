#include "uthread.h"
#include "scheduler.h"

#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <setjmp.h>
#include <stdio.h>

#define JB_SP 6
#define JB_PC 7

// ===== Thread Definition =====
static Thread threads[UTHREAD_MAX_THREADS];
static int current_tid = 0;
static int initialized = 0;
static struct itimerval timer;
static sigset_t uthread_sigset;
static int quantum_usec = 0;

// ===== Accessors for scheduler =====
Thread* get_threads(void) { return threads; }
int get_current_tid(void) { return current_tid; }
void set_current_tid(int tid) { current_tid = tid; }
sigset_t* get_uthread_sigset(void) { return &uthread_sigset; }

// ===== Init Threading System =====
int uthread_system_init(int quantum_usecs) {
    if (initialized || quantum_usecs <= 0 || quantum_usecs > 1000000) {
        fprintf(stderr, "uthread_system_init: invalid input or already initialized\n");
        return -1;
    }

    initialized = 1;
    quantum_usec = quantum_usecs;

    threads[0].tid = 0;
    threads[0].state = RUNNING;
    threads[0].entry = NULL;
    threads[0].stack = NULL;
    current_tid = 0;

    sigemptyset(&uthread_sigset);
    sigaddset(&uthread_sigset, SIGVTALRM);

    struct sigaction sa;
    sa.sa_handler = schedule;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER;

    if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
        perror("sigaction failed");
        return -1;
    }

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = quantum_usecs;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = quantum_usecs;

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL) < 0) {
        perror("setitimer failed");
        return -1;
    }

    printf("uthread_system_init: initialized with quantum = %d µs\n", quantum_usecs);
    return 0;
}

// ===== Create New Thread =====
int uthread_create(uthread_entry entry_func) {
    if (!initialized || entry_func == NULL) {
        fprintf(stderr, "uthread_create: not initialized or entry_func is NULL\n");
        return -1;
    }

    // Find an available TID
    int tid = -1;
    for (int i = 1; i < UTHREAD_MAX_THREADS; ++i) {
        if (threads[i].tid == -1) {
            tid = i;
            break;
        }
    }

    if (tid == -1) {
        fprintf(stderr, "uthread_create: no available TID slots\n");
        return -1;
    }

    void* stack = malloc(UTHREAD_STACK_BYTES);
    if (!stack) {
        fprintf(stderr, "uthread_create: malloc failed for thread %d\n", tid);
        return -1;
    }

    threads[tid].tid = tid;
    threads[tid].state = READY;
    threads[tid].entry = entry_func;
    threads[tid].stack = stack;

    printf("uthread_create: creating thread %d\n", tid);

    if (sigsetjmp(threads[tid].context, 1) == 0) {
        printf("uthread_create: saved context for thread %d\n", tid);
        return tid;
    }

    // Never reached during thread creation.
    return tid;
}
