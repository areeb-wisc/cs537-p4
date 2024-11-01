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

typedef struct _minheap {
    struct proc* arr;
    int size;
    int maxsize;
} minheap;

void init(minheap* h, int maxsize) {
    h->size = 0;
    h->maxsize = maxsize;
    h->arr = (struct proc*)malloc(h->maxsize * sizeof(struct proc));
}

int parent(int idx) {
    return (idx - 1)/2;
}

int compare(struct proc* p1, struct proc* p2) {
    
    if (p1->pass != p2->pass)
        return p1->pass < p2->pass ? -1 : 1;
    
    if (p1->runtime != p2->runtime)
        return p1->runtime < p2->runtime ? -1 : 1;
    
    if (p1->pid != p2->pid)
        return p1->pid < p2->pid ? -1 : 1;
    
    return 0;
}

int greater(struct proc* p1, struct proc* p2) {
    return compare(p1,p2) > 0;
}

void swap(struct proc* p1, struct proc* p2) {
    struct proc temp = *p1;
    *p1 = *p2;
    *p2 = temp;
}

void heapify(minheap* h, int idx) {
    int smallest = idx;
    int left = 2*idx + 1, right = left + 1;
    if (left < h->size && greater(&h->arr[smallest],&h->arr[left]))
        smallest = left;
    if (right < h->size && greater(&h->arr[smallest],&h->arr[right]))
        smallest = right;
    if (smallest != idx) {
        swap(&h->arr[idx],&h->arr[smallest]);
        heapify(h, smallest);
    }
}

int push(minheap* h, struct proc p) {
    if (h->size == h->maxsize)
        return -1;
    int idx = h->size;
    h->arr[idx] = p;
    while (idx != 0 && greater(&h->arr[parent(idx)],&h->arr[idx])) {
        swap(&h->arr[idx],&h->arr[parent(idx)]);
        idx = parent(idx);
    }
    h->size++;
    return 0;
}

struct proc getmin(minheap* h) {
    return h->arr[0];
}

void pop(minheap* h) {
    swap(&h->arr[0],&h->arr[h->size - 1]);
    h->size--;
    heapify(h,0);
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