#include "scheduler.h"
#include "uthread.h"
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>

#define QUEUE_SIZE UTHREAD_MAX_THREADS

// ======== Simple Circular Queue for TIDs ========= //
static int ready_queue[QUEUE_SIZE];
static int front = 0, rear = 0;
static int size = 0;

// ========== Queue Utilities ========== //
void enqueue_ready(int tid) {
    if (size >= QUEUE_SIZE) return;  // Queue full
    ready_queue[rear] = tid;
    rear = (rear + 1) % QUEUE_SIZE;
    size++;
}

int dequeue_ready() {
    if (size == 0) return -1;
    int tid = ready_queue[front];
    front = (front + 1) % QUEUE_SIZE;
    size--;
    return tid;
}

// ========== Preemptive Scheduler ========== //
void schedule(int sig) {
    (void)sig;  // suppress unused parameter warning    
    sigset_t* block_set = get_uthread_sigset();

    sigprocmask(SIG_BLOCK, block_set, NULL);  // block SIGVTALRM

    Thread* threads = get_threads();
    int current_tid = get_current_tid();
    Thread* current = &threads[current_tid];

    if (sigsetjmp(current->context, 1) == 0) {
        if (current->state == RUNNING) {
            current->state = READY;
            enqueue_ready(current_tid);  // re-enqueue current thread
        }

        int next_tid = dequeue_ready();
        if (next_tid != -1) {
            set_current_tid(next_tid);
            threads[next_tid].state = RUNNING;
            sigprocmask(SIG_UNBLOCK, block_set, NULL);  // unblock signals
            siglongjmp(threads[next_tid].context, 1);    // switch to next thread
        }

        // If no other thread is READY, continue running current
        set_current_tid(current_tid);
        current->state = RUNNING;
        sigprocmask(SIG_UNBLOCK, block_set, NULL);
    }
}
