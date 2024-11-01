#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct proc {
  int pid;                     // Process ID
  char name[16];               // Process name (debugging)
  // stride scheduling
  int tickets;                 // number of tickets
  int stride;                  // this process's stride
  int pass;                    // this process's pass
  int remain;                  // this process's remainining stride
  int last_scheduled;           // the tick at which this process was last scheduled
  int last_interrupted;        // the tick at which this process was last interrupted
  int runtime;                 // total ticks this process has run for
};

struct proc* makeproc(int pid, char* name, int pass, int runtime) {
    struct proc* nproc = (struct proc*)malloc(sizeof(struct proc));
    nproc->pid = pid;
    strncpy(nproc->name, name, strlen(name));
    nproc->pass = pass;
    nproc->runtime = runtime;
    return nproc;
}

typedef struct _minheap {
    struct proc* arr;
    int size;
    int maxsize;
} minheap;

void init(minheap** h, int maxsize) {
    *h = (minheap*)malloc(sizeof(minheap));
    (*h)->size = 0;
    (*h)->maxsize = maxsize;
    (*h)->arr = (struct proc*)malloc((*h)->maxsize * sizeof(struct proc));
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

int push(minheap* h, struct proc* p) {
    if (h->size == h->maxsize)
        return -1;
    int idx = h->size;
    h->arr[idx] = *p;
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

struct proc* getsorted(minheap* h) {
    int n = h->size;
    struct proc* sorted = (struct proc*)malloc(n * sizeof(struct proc));
    for (int i = 0; i < n; i++) {
        struct proc popped = getmin(h);
        sorted[i] = popped;
        pop(h);
    }
    for (int i = 0; i < n; i++) {
        struct proc nproc = sorted[i];
        push(h, &nproc);
    }
    return sorted;
}

void display(struct proc* arr, int size) {
    for (int i = 0; i < size; i++)
        printf("[name:%s | pass:%d | rtime:%d | PID:%d]\n", arr[i].name, arr[i].pass, arr[i].runtime, arr[i].pid);
    printf("\n");
}

int getrand(int min, int max) {
    return min + (rand() % (max - min + 1));
}

int hsize = 12;

int myrand() {
    return getrand(1,hsize);
}

int main() {

    minheap* h;
    init(&h, hsize);

    // for (int i = 0; i < hsize; i++)
    //     push(h, makeproc(myrand(),"rand",myrand(),myrand()));

    push(h, makeproc(8,"P8",1,5));
    push(h, makeproc(7,"P7",7,4));
    push(h, makeproc(1,"P1",7,5));
    push(h, makeproc(2,"P2",7,3));
    push(h, makeproc(4,"P3",6,2));
    push(h, makeproc(3,"P4",6,2));
    push(h, makeproc(5,"P5",6,3));
    push(h, makeproc(6,"P6",6,3));
    push(h, makeproc(9,"P9",9,5));
    push(h, makeproc(10,"P10",10,2));
    push(h, makeproc(11,"P11",11,0));
    push(h, makeproc(12,"P12",12,5));


    display(getsorted(h),hsize);

}