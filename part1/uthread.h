/*
 * User-Level Threading Library (Upwind Threads)
 * Research Analyst Technical Challenge
 *
 * This header defines the API for a simplified user-level thread library.
 * Candidates must implement this interface.
 */
#ifndef UTHREAD_H
#define UTHREAD_H

#include <stddef.h>
#include <setjmp.h>
#include <signal.h>

#define UTHREAD_MAX_THREADS 10    /* Maximum number of concurrent threads */
#define UTHREAD_STACK_BYTES 4096  /* Stack size per thread in bytes */

typedef void (*uthread_entry)(void);

/* ===========================
   Initialization & Management
   =========================== */

/**
 * @brief Initializes the threading system.
 *
 * Must be called before any other function in this library. The function
 * sets up the main thread (TID 0) as the first running thread.
 *
 * @param quantum_usecs The time slice (quantum) in microseconds for thread execution.
 * @return 0 on success, -1 on failure (invalid quantum value).
 */
int uthread_system_init(int quantum_usecs);

/**
 * @brief Creates a new thread and schedules it for execution.
 *
 * The thread starts executing at the given entry function. The new thread
 * is placed in the READY queue and will be scheduled when its turn arrives.
 *
 * @param entry_func The function where the thread execution starts.
 * @return Thread ID (TID) on success, -1 on failure (e.g., too many threads).
 */
int uthread_create(uthread_entry entry_func);

/**
 * @brief Terminates the specified thread.
 *
 * Frees resources associated with the thread. If the main thread (TID 0)
 * is terminated, the entire process will exit.
 *
 * @param tid The ID of the thread to terminate.
 * @return 0 on success, -1 on failure (invalid TID or attempting to terminate the main thread).
 */
int uthread_exit(int tid);

/* ===========================
   Thread State Control
   =========================== */

/**
 * @brief Suspends a thread, moving it to the BLOCKED state.
 *
 * A blocked thread can only resume execution via `uthread_unblock()`. The main thread
 * (TID 0) cannot be blocked. If a thread blocks itself, scheduling should occur immediately.
 *
 * @param tid The ID of the thread to block.
 * @return 0 on success, -1 on failure (invalid TID or blocking the main thread).
 */
int uthread_block(int tid);

/**
 * @brief Resumes execution of a previously blocked thread.
 *
 * Moves a thread from BLOCKED to READY state. If the thread is already running or ready,
 * this function has no effect.
 *
 * @param tid The ID of the thread to unblock.
 * @return 0 on success, -1 on failure (invalid TID or thread not in BLOCKED state).
 */
int uthread_unblock(int tid);

/**
 * @brief Puts the calling thread to sleep for a specified number of quantum cycles.
 *
 * The thread will automatically transition back to READY state after the sleep duration.
 * The main thread (TID 0) cannot call this function.
 *
 * @param num_quantums The number of quantums to sleep.
 * @return 0 on success, -1 on failure (invalid parameters or called from the main thread).
 */
int uthread_sleep_quantums(int num_quantums);

/* ===========================
   Internal Data Structures
   =========================== */

typedef enum {
    READY,
    RUNNING,
    BLOCKED
} thread_state_t;

typedef struct {
    int tid;
    thread_state_t state;
    uthread_entry entry;
    void* stack;
    sigjmp_buf context;
    int context_valid;
} Thread;

// Global sleep table - declared here, defined in implementation
extern int sleep_table[UTHREAD_MAX_THREADS];

// Accessor functions for internal use
Thread* get_threads(void);
int get_current_tid(void);
void set_current_tid(int tid);
sigset_t* get_uthread_sigset(void);
int* get_sleep_table(void);
void thread_func_wrapper(void);

#endif /* UTHREAD_H */