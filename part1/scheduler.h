#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "uthread.h"

void schedule();
void enqueue_ready(int tid);
int dequeue_ready(void);

#endif
