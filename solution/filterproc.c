#include <stdio.h>
#include <stdlib.h>
#include "proc.h"
#include "param.h"
#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

typedef struct __procarray {
    struct proc* procs;
    int size;
    int maxsize;
} procarray;

int init(procarray* parr, int maxsize) {
    parr = (procarray*)malloc(sizeof(procarray));
    parr->size = 0;
    parr->maxsize = maxsize;
    parr->procs = (struct proc*)malloc(parr->maxsize * sizeof(struct proc));
    return 0;
}

int append(procarray* parr, struct proc newproc) {
    if (parr->size == parr->maxsize)
        return -1;
    parr->procs[parr->size] = newproc;
    parr->size++;
    return 0;
}

int freearray(procarray* parr) {
    free(parr->procs);
    free(parr);
    return 0;
}

struct proc* getmin() {

    struct proc* p;
    struct proc* ans = (struct proc*)malloc(sizeof(struct proc));

    procarray* runnables; // collect runnable processes
    init(runnables, NPROC);
    
    int minimum_pass = 10000; // keep track of minimum pass value
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->state == RUNNABLE) {
            if (p->pass < minimum_pass)
                minimum_pass = p->pass;
            append(runnables, *p);
        }
    }

    if (runnables->size == 1) {
        *ans = runnables->procs[0];
        freearray(runnables);
        return ans;
    }

    procarray* minpass; // collect processes with same lowest pass 
    init(minpass,runnables->size);
    int min_runtime = 10000; // keep track of min runtime among these

    for (p = runnables->procs; p < &runnables->procs[runnables->size]; p++) {
        if (p->pass == minimum_pass) {
            if (p->runtime < min_runtime)
                min_runtime = min_runtime;
            append(minpass, *p);
        }
    }

    freearray(runnables);
    if (minpass->size == 1) {
        *ans = minpass->procs[0];
        freearray(minpass);
        return ans;
    }

    procarray* minruntime;
    init(minruntime, minpass->size);
    struct proc* minpid = minpass->procs;

    for (p = minpass->procs; p < &minpass->procs[minpass->size]; p++) {
        if (p->runtime == min_runtime) {
            if (p->pid < minpid->pid)
                minpid = p;
            append(minruntime, *p);
        }
    }

    freearray(minpass);
    if (minruntime->size == 1)
        *ans = minruntime->procs[0];
    else
        *ans = *minpid;

    freearray(minruntime);

    return ans;

}

int main() {

    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    struct cpu* c = mycpu();
    struct proc* p = getmin();


    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;
    
    acquire(&ptable.lock);

    swtch(&(c->scheduler), p->context);
    switchkvm();

    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;
    
    release(&ptable.lock);
}