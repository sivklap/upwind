/*
 * User-Level Threading Library 
 * Implementation file
 */

#include "uthread.h"
#include "scheduler.h"

#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <setjmp.h>
#include <stdio.h>

// Globals
static Thread threads[UTHREAD_MAX_THREADS];
static int current_tid = 0;
static int initialized = 0;
static struct itimerval timer;
static sigset_t uthread_sigset;
static int quantum_usec = 0;
int sleep_table[UTHREAD_MAX_THREADS] = {0};

// Accessor Functions
Thread* get_threads(void) { return threads; }
int get_current_tid(void) { return current_tid; }
void set_current_tid(int tid) { current_tid = tid; }
sigset_t* get_uthread_sigset(void) { return &uthread_sigset; }
int* get_sleep_table(void) { return sleep_table; }

void thread_func_wrapper() {
    int tid = get_current_tid();
    Thread* threads = get_threads();

    printf("[wrapper] Starting thread %d\n", tid);

    if (tid < 0 || tid >= UTHREAD_MAX_THREADS || threads[tid].tid == -1) {
        fprintf(stderr, "[thread_func_wrapper] FATAL: invalid TID %d\n", tid);
        exit(1);
    }

    if (threads[tid].entry) {
        threads[tid].entry();
    }

    printf("[wrapper] Thread %d finished, exiting\n", tid);
    uthread_exit(tid);
}

/* ===========================
   Initialization & Management
   =========================== */

int uthread_system_init(int quantum_usecs) {
    if (initialized || quantum_usecs <= 0 || quantum_usecs > 1000000) {
        fprintf(stderr, "uthread_system_init: invalid quantum value\n");
        return -1;
    }

    initialized = 1;
    quantum_usec = quantum_usecs;

    // Initialize all threads
    for (int i = 0; i < UTHREAD_MAX_THREADS; ++i) {
        threads[i].tid = -1;
        threads[i].state = BLOCKED;
        threads[i].entry = NULL;
        threads[i].stack = NULL;
        threads[i].context_valid = 0;
        sleep_table[i] = 0;
    }

    // Set up main thread (TID 0) as specified
    threads[0].tid = 0;
    threads[0].state = RUNNING;
    threads[0].entry = NULL;
    threads[0].stack = NULL;
    threads[0].context_valid = 1;
    current_tid = 0;

    // Set up signal handling for preemption
    sigemptyset(&uthread_sigset);
    sigaddset(&uthread_sigset, SIGVTALRM);

    struct sigaction sa;
    sa.sa_handler = schedule;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
        perror("sigaction failed");
        return -1;
    }

    // Set up timer for preemptive scheduling
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = quantum_usec;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = quantum_usec;

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL) < 0) {
        perror("setitimer failed");
        return -1;
    }

    printf("uthread_system_init: initialized with quantum = %d µs\n", quantum_usecs);
    return 0;
}

int uthread_create(uthread_entry entry_func) {
    if (!initialized || !entry_func) {
        fprintf(stderr, "uthread_create: system not initialized or invalid entry function\n");
        return -1;
    }

    // Block signals during thread creation
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGVTALRM);
    sigprocmask(SIG_BLOCK, &set, NULL);

    int tid = -1;
    for (int i = 1; i < UTHREAD_MAX_THREADS; ++i) {
        if (threads[i].tid == -1) {
            tid = i;
            break;
        }
    }

    if (tid == -1) {
        fprintf(stderr, "uthread_create: too many threads\n");
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return -1;
    }

    // Initialize thread and place in READY queue
    threads[tid].tid = tid;
    threads[tid].state = READY;
    threads[tid].entry = entry_func;
    threads[tid].stack = NULL;
    threads[tid].context_valid = 0; // Will be set when first run
    sleep_table[tid] = 0;

    // Add to ready queue 
    enqueue_ready(tid);
    
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    printf("uthread_create: created thread %d\n", tid);
    return tid;
}

int uthread_exit(int tid) {
    if (!initialized || tid < 0 || tid >= UTHREAD_MAX_THREADS) {
        fprintf(stderr, "uthread_exit: invalid TID\n");
        return -1;
    }

    if (threads[tid].tid == -1) {
        fprintf(stderr, "uthread_exit: thread %d already terminated\n", tid);
        return -1;
    }

    // Special handling for main thread - exit entire process
    if (tid == 0) {
        printf("uthread_exit: terminating main thread - exiting process\n");
        exit(0);
    }

    // Clean up thread resources
    threads[tid].tid = -1;
    threads[tid].state = BLOCKED;
    threads[tid].entry = NULL;
    threads[tid].stack = NULL;
    memset(&threads[tid].context, 0, sizeof(sigjmp_buf));
    threads[tid].context_valid = 0;
    sleep_table[tid] = 0;
    
    remove_tid_from_ready_queue(tid);

    printf("uthread_exit: thread %d terminated\n", tid);

    // If terminating current thread, schedule next 
    if (tid == current_tid) {
        schedule(0);
        __builtin_unreachable();
    }

    return 0;
}

/* ===========================
   Thread State Control
   =========================== */

int uthread_block(int tid) {
    if (!initialized || tid < 0 || tid >= UTHREAD_MAX_THREADS || threads[tid].tid == -1) {
        fprintf(stderr, "uthread_block: invalid TID\n");
        return -1;
    }

    // Main thread cannot be blocked 
    if (tid == 0) {
        fprintf(stderr, "uthread_block: cannot block main thread\n");
        return -1;
    }

    threads[tid].state = BLOCKED;
    printf("uthread_block: thread %d moved to BLOCKED state\n", tid);

    // If thread blocks itself, scheduling occurs immediately
    if (tid == current_tid) {
        threads[tid].context_valid = 0; // Mark for restart to avoid stack issues
        schedule(0);
        printf("uthread_block: thread %d resumed from block\n", tid);
    }

    return 0;
}

int uthread_unblock(int tid) {
    if (!initialized || tid < 0 || tid >= UTHREAD_MAX_THREADS || threads[tid].tid == -1) {
        fprintf(stderr, "uthread_unblock: invalid TID\n");
        return -1;
    }

    // No effect if thread is already running or ready
    if (threads[tid].state == RUNNING || threads[tid].state == READY) {
        printf("uthread_unblock: thread %d already running/ready - no effect\n", tid);
        return 0;
    }

    if (threads[tid].state != BLOCKED) {
        fprintf(stderr, "uthread_unblock: thread not in BLOCKED state\n");
        return -1;
    }

    // Move from BLOCKED to READY state and place at end of queue
    threads[tid].state = READY;
    sleep_table[tid] = 0; // Clear any sleep timer
    enqueue_ready(tid);
    printf("uthread_unblock: thread %d moved to READY state\n", tid);
    
    return 0;
}

int uthread_sleep_quantums(int num_quantums) {
    if (!initialized || num_quantums <= 0) {
        fprintf(stderr, "uthread_sleep_quantums: invalid parameters\n");
        return -1;
    }

    // Main thread cannot call this function
    if (get_current_tid() == 0) {
        fprintf(stderr, "uthread_sleep_quantums: main thread cannot sleep\n");
        return -1;
    }

    int tid = get_current_tid();
    Thread* t = &threads[tid];

    t->state = BLOCKED;
    sleep_table[tid] = num_quantums;
    printf("uthread_sleep_quantums: thread %d sleeping for %d quantums\n", tid, num_quantums);

    // Mark for restart to avoid stack corruption issues
    t->context_valid = 0;
    schedule(0);
    
    printf("uthread_sleep_quantums: thread %d resumed from sleep\n", tid);
    return 0;
}