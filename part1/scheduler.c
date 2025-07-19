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

// For launching new threads
static int new_thread_launch = 0;
static int new_thread_id = -1;

// ===== Enqueue Ready Thread =====
void enqueue_ready(int tid) {
    if (size >= QUEUE_SIZE || tid < 0 || tid >= UTHREAD_MAX_THREADS)
        return;

    Thread* threads = get_threads();
    if (threads[tid].tid == -1 || threads[tid].state != READY)
        return;

    // Check if already in queue
    for (int i = 0; i < size; i++) {
        int idx = (front + i) % QUEUE_SIZE;
        if (ready_queue[idx] == tid) {
            return; // Already in queue
        }
    }

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

        if (tid >= 0 && tid < UTHREAD_MAX_THREADS &&
            threads[tid].tid != -1 &&
            threads[tid].state == READY) {
            return tid;
        }
    }

    return -1;
}

int remove_tid_from_ready_queue(int tid) {
    int new_queue[UTHREAD_MAX_THREADS];
    int new_size = 0;
    
    for (int i = 0; i < size; ++i) {
        int idx = (front + i) % QUEUE_SIZE;
        if (ready_queue[idx] != tid) {
            new_queue[new_size++] = ready_queue[idx];
        }
    }

    front = 0;
    rear = new_size;
    size = new_size;
    for (int i = 0; i < size; ++i) {
        ready_queue[i] = new_queue[i];
    }

    return 0;
}

// ===== Scheduler =====
void schedule(int sig) {
    (void)sig;

    Thread* threads = get_threads();
    int curr_tid = get_current_tid();

    // Handle new thread launch
    if (new_thread_launch) {
        new_thread_launch = 0;
        int tid = new_thread_id;
        new_thread_id = -1;
        
        set_current_tid(tid);
        threads[tid].state = RUNNING;
        threads[tid].context_valid = 1;
        
        printf("[schedule] Launching new thread %d\n", tid);
        thread_func_wrapper();
        __builtin_unreachable();
    }

    // Save current thread context if needed
    if (curr_tid >= 0 && curr_tid < UTHREAD_MAX_THREADS && 
        threads[curr_tid].tid != -1 && threads[curr_tid].context_valid) {
        
        if (sigsetjmp(threads[curr_tid].context, 1) == 1) {
            // We just got resumed
            return;
        }

        // If still running, move it to READY
        if (threads[curr_tid].state == RUNNING) {
            threads[curr_tid].state = READY;
            enqueue_ready(curr_tid);
            printf("[schedule] Thread %d moved to READY\n", curr_tid);
        }
    }

    // Wake up sleeping threads
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

    // Pick next thread to run
    int next_tid = -1;
    do {
        next_tid = dequeue_ready();
    } while (next_tid != -1 && threads[next_tid].tid == -1);

    if (next_tid == -1) {
        fprintf(stderr, "[schedule] No READY thread found. Exiting.\n");
        exit(1);
    }

    set_current_tid(next_tid);
    threads[next_tid].state = RUNNING;
    printf("[schedule] Switching to thread %d (context_valid = %d)\n",
           next_tid, threads[next_tid].context_valid);

    if (!threads[next_tid].context_valid) {
        // First time running this thread
        if (next_tid == 0) {
            // Main thread - just mark as valid and return
            threads[next_tid].context_valid = 1;
            return;
        }
        
        // Launch new thread using safe method
        new_thread_launch = 1;
        new_thread_id = next_tid;
        schedule(0); // Recursive call
        return;
    }

    // Resume existing thread
    siglongjmp(threads[next_tid].context, 1);
}