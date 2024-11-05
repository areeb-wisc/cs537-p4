#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "pstat.h"

int global_tickets = 0;
int global_pass = 0;
int global_stride = 0;
int last_global_pass_update = 0;

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

int
getticks(void)
{
  uint xticks;
  // cprintf("acquiring ticklock\n");
//  acquire(&tickslock);
  xticks = ticks;
  // release(&tickslock);
  // cprintf("released ticklock\n");
  return xticks;
}

void
global_tickets_update(int delta) {
  global_tickets += delta;
  // cprintf("delta: %d\n", delta);
  // cprintf("global_tickets: %d\n", global_tickets);
  if (global_tickets == 0)
    global_stride = 0;
  else
    global_stride = STRIDE1/global_tickets;
}

void
global_pass_update(void) {
  int elapsed = getticks() - last_global_pass_update;
  last_global_pass_update += elapsed;
  global_pass += (global_stride * elapsed);
  // cprintf("global pass: %d\n", global_pass);
}


// Only called when caller holds ptable lock
void
process_join(struct proc* p) {
  // cprintf("process_join\n");
  // cprintf("PID: %d name: %s joining with tickets: %d\n", p->pid, p->name, p->tickets);
  global_pass_update();
  p->pass = global_pass + p->remain;
  global_tickets_update(p->tickets);
}

void process_leave(struct proc* p) {
  
  // cprintf("process_leave\n");
  // cprintf("PID: %d name: %s leaving with tickets: %d\n", p->pid, p->name, p->tickets);

  // int previously_holding = holding(&ptable.lock);
  // if (!previously_holding)
  //   acquire(&ptable.lock);

  global_pass_update();
  p->remain = p->pass - global_pass;
  global_tickets_update(-1*(p->tickets));
  
  // if (!previously_holding)
  //   release(&ptable.lock);

  // cprintf("p->remain: %d\n", p->remain);
  return;
}

void
set_tickets(struct proc* p, int newtickets) {

  cprintf("set_tickets(%d)\n", newtickets);
  int oldtickets = p->tickets;

  int previously_holding = holding(&ptable.lock);
  if (!previously_holding)
    acquire(&ptable.lock);

  process_leave(p);

  int newstride = STRIDE1/newtickets;
  int newremain = (p->remain * newstride) / p->stride;

  p->tickets = newtickets;
  p->stride = newstride;
  p->remain = newremain;

  process_join(p);

  if (!previously_holding)
    release(&ptable.lock);

  cprintf("PID: %d name: %s oldtickets: %d newtickets: %d\n", p->pid, p->name, oldtickets, newtickets);
}

int
get_pinfo(struct pstat* data)
{
  cprintf("get_pinfo()\n");

  acquire(&ptable.lock);
  for (int i = 0; i < NPROC; i++) {
    struct proc p = ptable.proc[i];
    data->inuse[i] = p.state != UNUSED;
    data->tickets[i] = p.tickets;
    data->pid[i] = p.pid;
    data->pass[i] = p.pass;
    data->remain[i] = p.remain;
    data->stride[i] = p.stride;
    data->rtime[i] = p.runtime;
  }
  release(&ptable.lock);

  return 0;

}

void
print_stats() {
  cprintf("pinfo:\n");
  struct pstat data;
  get_pinfo(&data);
  cprintf("Inuse\tPID\tTickets\tPass\tStride\tRuntime\n");
  for (int i = 0; i < NPROC; i++) {
    if (data.tickets[i] > 0)
      cprintf("%d\t%d\t%d\t%d\t%d\t%d\n", data.inuse[i], data.pid[i], data.tickets[i], data.pass[i], data.stride[i], data.rtime[i]);
  }
  cprintf("\n");
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  cprintf("allocproc acquiring lock\n");
  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  cprintf("allocproc released lock\n");
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  p->last_scheduled = 0;
  p->last_interrupted = 0;
  p->runtime = 0;

  if (stride_scheduler) {
    p->tickets = TICKETS_INIT;
    p->stride = STRIDE1/p->tickets;
    p->pass = 0;
    p->remain = p->stride;
    // cprintf("before joining PID: %d, name: %s | tickets: %d\n", p->pid, p->name, p->tickets);
    process_join(p);
  }

  cprintf("allocproc released lock\n");
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  cprintf("fork()\n");
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  cprintf("allocproc() success\n");
  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    if (stride_scheduler) {
        acquire(&ptable.lock);
        process_leave(np);
        release(&ptable.lock);
    }
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  if (stride_scheduler)
    process_leave(curproc);

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;

  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

// ----------------------STRIDE SCHEDULER HELPERS START -----------------------------
typedef struct _minheap {
    struct proc arr[NPROC];
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
    if (h->size == NPROC)
        return -1;
    int idx = h->size;
    h->arr[idx] = *p;
    while (idx != 0 && greater(&h->arr[parent(idx)],&h->arr[idx])) {
        swap(&h->arr[idx],&h->arr[parent(idx)]);
        idx = parent(idx);
    }
    // cprintf("address in heap = %p\n", &h->arr[idx]);
    h->size++;
    return 0;
}
struct proc* getmin(minheap* h) {
    return &h->arr[0];
}
// ----------------------STRIDE SCHEDULER HELPERS END -----------------------------

#ifdef STRIDE
int stride_scheduler = 1;
#elif RR
int stride_scheduler = 0;
#endif

void
sched_roundrobin(void) {

  cprintf("running RR scheduler\n");
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;) {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      
      p->last_scheduled = getticks();
      swtch(&(c->scheduler), p->context);
      switchkvm();

      p->runtime += (p->last_interrupted - p->last_scheduled);

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
  }
}

void
sched_stride(void) {

  cprintf("Running stride scheduler\n");
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  struct proc* prev = 0;
  for(;;) {
    // Enable interrupts on this processor.
    sti();

    // cprintf("scheduler acquiring lock\n");
    acquire(&ptable.lock);

    // find process with min pass, min rtime, min pid
    minheap heap;
    heap.size = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p->state == RUNNABLE) {
        push(&heap, p);
      }
    }

    if (heap.size > 0) {

      struct proc* minproc = getmin(&heap);
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->pid == minproc->pid)
          break;
      }
      if (p != prev) {
        // cprintf("chosen PID: %d | name: %s | tickets: %d | stride: %d | pass: %d | rtime: %d\n", p->pid, p->name, p->tickets, p->stride, p->pass, p->runtime);
        prev = p;
      }
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      // cprintf("scheduler switching to RUNNABLE process: %s at %d ticks\n", p->name, getticks());
      p->last_scheduled = getticks();
      swtch(&(c->scheduler), p->context);
      switchkvm();
      // cprintf("returned from PID: %d name: %s\n", p->pid, p->name);
      // cprintf("Is lock held: %d\n", holding(&ptable.lock));

      int elapsed = p->last_interrupted - p->last_scheduled;
      p->runtime += elapsed;
      p->pass += (elapsed * p->stride);

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
    // cprintf("scheduler released lock\n");
  }
}
//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  for (;;) {
    if (stride_scheduler)
      sched_stride();
    else
      sched_roundrobin();
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;

  p->last_interrupted = getticks();
  // cprintf("switching from PID: %d\n", p->pid);
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  struct proc* p = myproc();
  
  p->state = RUNNABLE;

  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  // cprintf("PID: %d sleeping\n", p->pid);
  if (stride_scheduler)
    process_leave(p);

  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == SLEEPING && p->chan == chan) {
      if (stride_scheduler)
        process_join(p);
      p->state = RUNNABLE;
    }
  }

}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  cprintf("kill()\n");
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING) {
        if (stride_scheduler)
          process_join(p);
        p->state = RUNNABLE;
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
