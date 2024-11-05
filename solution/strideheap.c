#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include "proc.h"

struct proc {
  int pid;                     // Process ID
  // stride scheduling
  int pass;                    // this process's pass
  int runtime;                 // total ticks this process has run for
};

typedef struct _minheap {
    struct proc arr[64];
    int size;
} minheap;

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

int push(minheap* h, struct proc* p) {
    int idx = h->size;
    h->arr[idx] = *p;
    while (idx != 0 && greater(&h->arr[parent(idx)],&h->arr[idx])) {
        swap(&h->arr[idx],&h->arr[parent(idx)]);
        idx = parent(idx);
    }
    h->size++;
    return 0;
}

struct proc* getmin(minheap* h) {
    return &h->arr[0];
}

// Delete later, not needed
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

// Delete later, not needed
void pop(minheap* h) {
    swap(&h->arr[0],&h->arr[h->size - 1]);
    h->size--;
    heapify(h,0);
}

struct proc* makeproc(int pid, int pass, int rtime) {
    struct proc* p = (struct proc*)malloc(sizeof(struct proc));
    p->pid = pid;
    p->pass = pass;
    p->runtime = rtime;
    return p;
}

int myrand() {
    return 1 + (rand() % 10);
}

struct proc* randproc() {
    return makeproc(myrand(),myrand(),myrand());
}

struct proc* getsorted(minheap* h) {
    int n = h->size;
    struct proc* sorted = (struct proc*)malloc(n * sizeof(struct proc));
    for (int i = 0; i < n; i++) {
        struct proc* minproc = getmin(h);
        sorted[i] = *minproc;
        pop(h);
    }
    for (int i = 0; i < n; i++)
        push(h, &sorted[i]);
    return sorted;
}

void displaysorted(minheap* h) {
    struct proc* sorted = getsorted(h);
    for (int i = 0; i < h->size; i++)
        printf("[PID: %d | pass: %d | rtime: %d]\n", sorted[i].pid, sorted[i].pass, sorted[i].runtime);
    printf("\n");
}


int main() {
    srand(0);
    struct proc* procs[10];
    for (int i = 0; i < 10; i++) {

        for (int j = 0; j < 10; j++)
            procs[j] = randproc();
        
        minheap heap;
        heap.size = 0;

        for (int j = 0; j < 10; j++)
            push(&heap, procs[j]);
        struct proc* minproc1 = getmin(&heap);

        struct proc* minproc2 = procs[0];
        for (int j = 1; j < 10; j++) {
            if (compare(procs[j], minproc2) < 0)
                minproc2 = procs[j];
        }

        // displaysorted(&heap);
        printf("minproc1 = [PID: %d | pass: %d | rtime: %d]\n\n", minproc1->pid, minproc1->pass, minproc1->runtime);
        printf("minproc2 = [PID: %d | pass: %d | rtime: %d]\n\n", minproc2->pid, minproc2->pass, minproc2->runtime);

    }
    return 0;
}