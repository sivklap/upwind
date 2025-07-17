#include "uthread.h"
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <setjmp.h>

#define JB_SP 6
#define JB_PC 7

typedef enum { READY, RUNNING, BLOCKED } ThreadState;

typedef struct {
    int tid;
    ThreadState state;
    uthread_entry entry;
    sigjmp_buf context;
    void* stack;
} Thread;

static Thread threads[UTHREAD_MAX_THREADS];
static int current_tid = 0;
static int quantum_usec;
static int initialized = 0;

static sigset_t sigset;
static struct itimerval timer;
static int sleep_table[UTHREAD_MAX_THREADS] = {0};

static void schedule(int sig);

static void setup_timer() {
    timer.it_value.tv_sec = quantum_usec / 1000000;
    timer.it_value.tv_usec = quantum_usec % 1000000;
    timer.it_interval = timer.it_value;
    setitimer(ITIMER_VIRTUAL, &timer, NULL);
}

int uthread_system_init(void) {
    if (initialized) return -1;

    quantum_usec = 100000; // 100ms 
    initialized = 1;

    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);

    struct sigaction sa = {0};
    sa.sa_handler = schedule;
    sigaction(SIGVTALRM, &sa, NULL);

    threads[0].tid = 0;
    threads[0].state = RUNNING;
    threads[0].stack = NULL;

    if (sigsetjmp(threads[0].context, 1) != 0) {
        return 0;
    }

    setup_timer();
    return 0;
}

int uthread_create(uthread_entry entry_func) {
    for (int i = 1; i < UTHREAD_MAX_THREADS; ++i) {
        if (threads[i].stack == NULL) {
            threads[i].stack = malloc(UTHREAD_STACK_BYTES);
            if (!threads[i].stack) return -1;
            threads[i].entry = entry_func;
            threads[i].tid = i;
            threads[i].state = READY;

            if (sigsetjmp(threads[i].context, 1) == 0) {
                void* sp = (char*)threads[i].stack + UTHREAD_STACK_BYTES - sizeof(void*);
                *((void**)sp) = (void*)entry_func;
                ((long*)(threads[i].context))[JB_SP] = (long)sp;
                ((long*)(threads[i].context))[JB_PC] = (long)entry_func;
            }

            return i;
        }
    }
    return -1;
}

int uthread_exit(int tid) {
    if (tid <= 0 || tid >= UTHREAD_MAX_THREADS || threads[tid].stack == NULL)
        return -1;

    free(threads[tid].stack);
    threads[tid].stack = NULL;
    threads[tid].state = BLOCKED;
    if (tid == current_tid) {
        raise(SIGVTALRM);
    }
    return 0;
}

int uthread_block(int tid) {
    if (tid <= 0 || tid >= UTHREAD_MAX_THREADS || threads[tid].state != READY)
        return -1;

    threads[tid].state = BLOCKED;
    if (tid == current_tid) raise(SIGVTALRM);
    return 0;
}

int uthread_unblock(int tid) {
    if (tid <= 0 || tid >= UTHREAD_MAX_THREADS) return -1;
    if (threads[tid].state == BLOCKED && sleep_table[tid] == 0) {
        threads[tid].state = READY;
        return 0;
    }
    return -1;
}

int uthread_sleep_quantums(int num_quantums) {
    if (current_tid == 0 || num_quantums <= 0) return -1;
    sleep_table[current_tid] = num_quantums;
    threads[current_tid].state = BLOCKED;
    raise(SIGVTALRM);
    return 0;
}

static void schedule(int sig) {
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    if (sigsetjmp(threads[current_tid].context, 1) != 0) {
        sigprocmask(SIG_UNBLOCK, &sigset, NULL);
        return;
    }

    for (int i = 0; i < UTHREAD_MAX_THREADS; ++i) {
        if (threads[i].state == BLOCKED && sleep_table[i] > 0) {
            sleep_table[i]--;
            if (sleep_table[i] == 0) {
                threads[i].state = READY;
            }
        }
    }

    for (int i = 1; i < UTHREAD_MAX_THREADS; ++i) {
        int next_tid = (current_tid + i) % UTHREAD_MAX_THREADS;
        if (threads[next_tid].state == READY) {
            int prev_tid = current_tid;
            current_tid = next_tid;
            threads[current_tid].state = RUNNING;
            if (threads[prev_tid].state == RUNNING)
                threads[prev_tid].state = READY;
            siglongjmp(threads[current_tid].context, 1);
        }
    }

    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}
