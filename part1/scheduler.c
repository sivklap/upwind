#include "scheduler.h"
#include "uthread.h"
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>

#define QUEUE_SIZE UTHREAD_MAX_THREADS

// ===== Ready Queue =====
static int ready_queue[QUEUE_SIZE];
static int front = 0, rear = 0, size = 0;

// ===== Enqueue Ready Thread =====
void enqueue_ready(int tid) {
    if (size >= QUEUE_SIZE || tid < 0 || tid >= UTHREAD_MAX_THREADS)
        return;

    Thread* threads = get_threads();
    if (threads[tid].tid == -1 || threads[tid].state != READY)
        return;

    ready_queue[rear] = tid;
    rear = (rear + 1) % QUEUE_SIZE;
    size++;
}


// ===== Dequeue Next READY Thread =====
int dequeue_ready() {
    Thread* threads = get_threads();

    while (size > 0) {
        int tid = ready_queue[front];
        front = (front + 1) % QUEUE_SIZE;
        size--;

        // Skip invalid or dead threads
        if (tid >= 0 && tid < UTHREAD_MAX_THREADS &&
            threads[tid].tid != -1 &&
            threads[tid].state == READY) {
            return tid;
        } else {
            printf("[dequeue_ready] Skipped invalid/dead thread %d\n", tid);
        }
    }

    return -1;
}
// ===== First-Time Thread Launcher =====
void thread_bootstrap() {
    sigset_t* block_set = get_uthread_sigset();
    sigprocmask(SIG_UNBLOCK, block_set, NULL);

    thread_func_wrapper();  // Call the thread's entry function

    // Exit the thread if it returns
    int tid = get_current_tid();
    uthread_exit(tid);
}

// ===== Scheduler =====
void schedule(int sig) {
    (void)sig;
    sigset_t* block_set = get_uthread_sigset();
    sigprocmask(SIG_BLOCK, block_set, NULL);

    int curr_tid = get_current_tid();
    Thread* threads = get_threads();

    // === Handle sleeping threads ===
    for (int i = 0; i < UTHREAD_MAX_THREADS; i++) {
        if (threads[i].tid != -1 && sleep_table[i] > 0 && threads[i].state == BLOCKED) {
            sleep_table[i]--;
            if (sleep_table[i] == 0) {
                threads[i].state = READY;
                enqueue_ready(i);
                printf("[schedule] Thread %d woke up from sleep\n", i);
            }
        }
    }

    // === Save current thread context ===
    if (threads[curr_tid].tid != -1) {
        if (sigsetjmp(threads[curr_tid].context, 1) == 1) {
            printf("[schedule] Resumed in thread %d\n", curr_tid);
            sigprocmask(SIG_UNBLOCK, block_set, NULL);
            return;
        }

        threads[curr_tid].context_valid = 1;

        if (threads[curr_tid].state == RUNNING) {
            threads[curr_tid].state = READY;
            enqueue_ready(curr_tid);
            printf("[schedule] Thread %d moved to READY\n", curr_tid);
        }
    }

    // === Get next valid READY thread ===
    int next_tid = -1;
    do {
        next_tid = dequeue_ready();
    } while (next_tid != -1 && threads[next_tid].tid == -1);  // skip dead threads

    if (next_tid == -1) {
        fprintf(stderr, "[schedule] No thread to run, exiting\n");
        exit(1);
    }

    set_current_tid(next_tid);
    threads[next_tid].state = RUNNING;
    printf("[schedule] Switching to thread %d (context_valid = %d)\n", next_tid, threads[next_tid].context_valid);

    // === First-time run? ===
    if (!threads[next_tid].context_valid) {
        threads[next_tid].context_valid = 1;
        if (next_tid == 0) {
            printf("[schedule] Resuming main thread\n");
            sigprocmask(SIG_UNBLOCK, block_set, NULL);
            return;
        }
        sigprocmask(SIG_UNBLOCK, block_set, NULL);
        thread_bootstrap();  // never returns
    }

    // === Resume saved context ===
    sigprocmask(SIG_UNBLOCK, block_set, NULL);

    if (threads[next_tid].tid == -1) {
    fprintf(stderr, "[schedule] FATAL: tried to resume dead thread %d\n", next_tid);
    __builtin_trap();
    }
    siglongjmp(threads[next_tid].context, 1);

    // Should never reach here
    __builtin_trap();
}
