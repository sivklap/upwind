#include "uthread.h"
#include "scheduler.h"

#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <setjmp.h>
#include <stdio.h>

// ===== Globals =====
static Thread threads[UTHREAD_MAX_THREADS];
static int current_tid = 0;
static int initialized = 0;
static struct itimerval timer;
static sigset_t uthread_sigset;
static int quantum_usec = 0;
int sleep_table[UTHREAD_MAX_THREADS] = {0};

// ===== Accessors =====
Thread* get_threads(void) { return threads; }

int get_current_tid(void) {
    return current_tid;
}

void set_current_tid(int tid) {
    current_tid = tid;
}

sigset_t* get_uthread_sigset(void) {
    return &uthread_sigset;
}

// ===== Init Context =====
void uthread_init_context(Thread* t, uthread_entry func) {
    t->entry = func;
    t->context_valid = 0;
}

void thread_func_wrapper() {
    int tid = get_current_tid();
    Thread* threads = get_threads();

    // Safety check: ensure this thread is valid
    if (tid < 0 || tid >= UTHREAD_MAX_THREADS || threads[tid].tid == -1) {
        fprintf(stderr, "[thread_func_wrapper] FATAL: invalid TID %d\n", tid);
        __builtin_trap();
    }

    set_current_tid(tid);  // ensure context consistency
    if (threads[tid].entry) {
        threads[tid].entry();
    }

    uthread_exit(tid);
}

// ===== Init System =====
int uthread_system_init(int quantum_usecs) {
    if (initialized || quantum_usecs <= 0 || quantum_usecs > 1000000) {
        fprintf(stderr, "uthread_system_init: invalid input or already initialized\n");
        return -1;
    }

    initialized = 1;
    quantum_usec = quantum_usecs;

    for (int i = 0; i < UTHREAD_MAX_THREADS; ++i) {
        threads[i].tid = -1;
        threads[i].state = BLOCKED;
        threads[i].entry = NULL;
        threads[i].stack = NULL;
        threads[i].context_valid = 0;
    }

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
    timer.it_value.tv_usec = quantum_usec;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = quantum_usec;

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL) < 0) {
        perror("setitimer failed");
        return -1;
    }

    if (sigprocmask(SIG_UNBLOCK, &uthread_sigset, NULL) < 0) {
        perror("sigprocmask unblock failed");
        return -1;
    }

    printf("uthread_system_init: initialized with quantum = %d µs\n", quantum_usecs);
    return 0;
}

// ===== Create Thread =====
int uthread_create(uthread_entry entry_func) {
    int tid = -1;
    for (int i = 1; i < UTHREAD_MAX_THREADS; ++i) {
        if (threads[i].tid == -1 && threads[i].state == BLOCKED) {
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
        fprintf(stderr, "uthread_create: failed to allocate stack\n");
        return -1;
    }

    threads[tid].tid = tid;
    threads[tid].state = READY;
    threads[tid].stack = stack;
    threads[tid].context_valid = 0;

    uthread_init_context(&threads[tid], entry_func);
    enqueue_ready(tid);
    return tid;
}

// ===== Exit =====
int uthread_exit(int tid) {
    if (!initialized || tid < 0 || tid >= UTHREAD_MAX_THREADS) {
        fprintf(stderr, "uthread_exit: invalid tid %d\n", tid);
        return -1;
    }

    sigset_t* sigset = get_uthread_sigset();
    sigprocmask(SIG_BLOCK, sigset, NULL);  // ⛔ prevent timer from firing while exiting

    if (tid == 0) {
        printf("uthread_exit: terminating main thread. Exiting.\n");
        exit(0);
    }

    Thread* threads = get_threads();
    threads[tid].tid = -1;
    threads[tid].state = BLOCKED;
    threads[tid].entry = NULL;
    memset(&threads[tid].context, 0, sizeof(sigjmp_buf));  // optional safety

    printf("uthread_exit: thread %d terminated\n", tid);

    int next_tid = -1;
    do {
        next_tid = dequeue_ready();
    } while (next_tid != -1 && threads[next_tid].tid == -1);  // skip dead threads

    if (next_tid == -1) {
        printf("uthread_exit: no ready threads left, exiting.\n");
        exit(0);
    }

    set_current_tid(next_tid);
    threads[next_tid].state = RUNNING;

    if (!threads[next_tid].context_valid) {
        threads[next_tid].context_valid = 1;
        sigprocmask(SIG_UNBLOCK, sigset, NULL);
        if (next_tid == 0) {
            printf("[uthread_exit] resuming main thread\n");
            return 0;
        }
        thread_bootstrap();  // never returns
    }

    sigprocmask(SIG_UNBLOCK, sigset, NULL);
    printf("uthread_exit: jumping to thread %d\n", next_tid);
    siglongjmp(threads[next_tid].context, 1);

    __builtin_trap();  // should never reach here
}


// ===== Block =====
int uthread_block(int tid) {
    if (!initialized || tid < 0 || tid >= UTHREAD_MAX_THREADS || threads[tid].tid == -1) {
        fprintf(stderr, "uthread_block: invalid tid %d\n", tid);
        return -1;
    }

    if (tid == 0) {
        fprintf(stderr, "uthread_block: cannot block main thread (tid 0)\n");
        return -1;
    }

    threads[tid].state = BLOCKED;
    printf("uthread_block: thread %d is now BLOCKED\n", tid);

    if (tid == current_tid) {
        if (sigsetjmp(threads[tid].context, 1) == 0) {
            schedule(0);
        }
    }

    return 0;
}

// ===== Unblock =====
int uthread_unblock(int tid) {
    if (!initialized || tid < 0 || tid >= UTHREAD_MAX_THREADS || threads[tid].tid == -1) {
        fprintf(stderr, "uthread_unblock: invalid tid %d\n", tid);
        return -1;
    }

    if (threads[tid].state != BLOCKED) {
        printf("uthread_unblock: thread %d is not in BLOCKED state\n", tid);
        return -1;
    }

    threads[tid].state = READY;
    enqueue_ready(tid);
    printf("uthread_unblock: thread %d is now READY\n", tid);
    return 0;
}

// ===== Sleep =====
int uthread_sleep_quantums(int num_quantums) {
    if (!initialized || num_quantums <= 0 || get_current_tid() == 0)
        return -1;

    int tid = get_current_tid();
    Thread* t = &get_threads()[tid];

    t->state = BLOCKED;
    sleep_table[tid] = num_quantums;
    printf("uthread_sleep_quantums: thread %d sleeping for %d quantums\n", tid, num_quantums);

    // Block timer while saving context
    sigset_t* sigset = get_uthread_sigset();
    sigprocmask(SIG_BLOCK, sigset, NULL);

    if (sigsetjmp(t->context, 1) == 1) {
        // Unblock and return to thread
        sigprocmask(SIG_UNBLOCK, sigset, NULL);
        return 0;
    }

    t->context_valid = 1;

    // Unblock before switching
    sigprocmask(SIG_UNBLOCK, sigset, NULL);
    schedule(0);
    return 0;
}
