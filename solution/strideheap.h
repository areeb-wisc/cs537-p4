# ifndef __STRIDE_HEAP_H
# define __STRIDE_HEAP_H

typedef struct _minheap {
    struct proc* arr;
    int size;
    int maxsize;
} minheap;

void init(minheap** h, int maxsize);
int parent(int idx);
int compare(struct proc* p1, struct proc* p2);
int greater(struct proc* p1, struct proc* p2);
void swap(struct proc* p1, struct proc* p2);
int push(minheap* h, struct proc* p);
struct proc getmin(minheap* h);
void freeheap(minheap* h);

// Delete later, not needed
void heapify(minheap* h, int idx);
// Delete later, not needed
void pop(minheap* h);
# endif