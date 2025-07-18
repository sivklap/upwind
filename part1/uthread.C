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

    return 0;
}

// ===== Create New Thread =====
int uthread_create(uthread_entry entry_func) {
    if (!initialized || entry_func == NULL) {
        return -1;
    }

    // Find an available TID
    int tid = -1;
    for (int i = 1; i < UTHREAD_MAX_THREADS; ++i) {
        if (threads[i].state != READY &&
            threads[i].state != RUNNING &&
            threads[i].state != BLOCKED) {
            tid = i;
            break;
        }
    }

    if (tid == -1) return -1;

    void* stack = malloc(UTHREAD_STACK_BYTES);
    if (!stack) return -1;

    threads[tid].tid = tid;
    threads[tid].state = READY;
    threads[tid].entry = entry_func;
    threads[tid].stack = stack;

    if (sigsetjmp(threads[tid].context, 1) == 0) {
        // Do nothing here: just store the context.
        // We'll jump into trampoline later via siglongjmp.
        return tid;
    }

    // Never reached during thread creation.
    return tid;
}
