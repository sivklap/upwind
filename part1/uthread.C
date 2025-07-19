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
int sleep_table[UTHREAD_MAX_THREADS] = {0};

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

    // Mark all thread slots as unused
    for (int i = 0; i < UTHREAD_MAX_THREADS; ++i) {
        threads[i].tid = -1;
        threads[i].state = BLOCKED;
        threads[i].entry = NULL;
        threads[i].stack = NULL;
    }

    // Set up main thread (TID 0)
    threads[0].tid = 0;
    threads[0].state = RUNNING;
    threads[0].entry = NULL;
    threads[0].stack = NULL;
    current_tid = 0;

    // Block SIGVTALRM when modifying thread states
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
        // Stack pointer and PC would be set when thread is launched
        printf("uthread_create: saved context for thread %d\n", tid);
        return tid;
    }

    // This point is only reached when thread starts running
    return tid;
}

int uthread_exit(int tid) {
    // Validate the TID
    if (!initialized || tid < 0 || tid >= UTHREAD_MAX_THREADS || threads[tid].tid == -1) {
        fprintf(stderr, "uthread_exit: invalid tid %d\n", tid);
        return -1;
    }

    // If main thread is exiting, terminate the process
    if (tid == 0) {
        printf("uthread_exit: terminating main thread. Exiting process.\n");
        exit(0);
    }

    // Free the stack if it was allocated
    if (threads[tid].stack != NULL) {
        free(threads[tid].stack);
        threads[tid].stack = NULL;
    }

    // Clear thread data
    threads[tid].tid = -1;
    threads[tid].state = BLOCKED;
    threads[tid].entry = NULL;
    memset(&threads[tid].context, 0, sizeof(sigjmp_buf));

    printf("uthread_exit: thread %d terminated\n", tid);
    return 0;
}

int uthread_block(int tid) {
    // Validate the TID
    if (!initialized || tid < 0 || tid >= UTHREAD_MAX_THREADS || threads[tid].tid == -1) {
        fprintf(stderr, "uthread_block: invalid tid %d\n", tid);
        return -1;
    }

    // Main thread cannot be blocked
    if (tid == 0) {
        fprintf(stderr, "uthread_block: cannot block main thread (tid 0)\n");
        return -1;
    }

    // Block the thread
    threads[tid].state = BLOCKED;
    printf("uthread_block: thread %d is now BLOCKED\n", tid);

    // If the thread blocks itself, trigger scheduling
    if (tid == current_tid) {
        sigsetjmp(threads[tid].context, 1);
        schedule(); // preemptive scheduling — switch to another thread
    }

    return 0;
}

int uthread_unblock(int tid) {
    // Validate TID
    if (!initialized || tid < 0 || tid >= UTHREAD_MAX_THREADS || threads[tid].tid == -1) {
        fprintf(stderr, "uthread_unblock: invalid tid %d\n", tid);
        return -1;
    }

    // Only unblock if the thread is currently BLOCKED
    if (threads[tid].state != BLOCKED) {
        printf("uthread_unblock: thread %d is not in BLOCKED state\n", tid);
        return -1;
    }

    threads[tid].state = READY;
    printf("uthread_unblock: thread %d is now READY\n", tid);
    return 0;
}

int uthread_sleep_quantums(int num_quantums) {
    // Validate input
    if (!initialized || num_quantums <= 0 || current_tid == 0) {
        fprintf(stderr, "uthread_sleep_quantums: invalid input or called from main thread\n");
        return -1;
    }

    // Mark the thread as BLOCKED and set sleep duration
    threads[current_tid].state = BLOCKED;
    sleep_table[current_tid] = num_quantums;
    printf("uthread_sleep_quantums: thread %d sleeping for %d quantums\n", current_tid, num_quantums);

    // Yield control to another thread via scheduler
    if (sigsetjmp(threads[current_tid].context, 1) == 0) {
        schedule();
    }

    return 0;
}
