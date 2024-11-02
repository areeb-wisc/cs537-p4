#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#ifdef STRIDE
int stride_scheduler = 1;
#elif RR
int stride_scheduler = 0;
#endif

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

  acquire(&ptable.lock);
  cprintf("allocproc() acquired ptable.lock\n");

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  cprintf("allocproc() released ptable.lock\n");
  return 0;

found:
  cprintf("allocproc() found UNUSED slot at idx: %d\n", p - ptable.proc);
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);
  cprintf("allocproc() released ptable.lock\n");

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
  cprintf("userinit() trying to get ptable.lock\n");
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  cprintf("made PID: %d name: %s RUNNABLE\n", p->pid, p->name);
  if (stride_scheduler) {
    cprintf("adding stride details for PID: %d in userinit()\n", p->pid);
    p->tickets = TICKETS_INIT;
    p->stride = STRIDE1/p->tickets;
    p->pass = 0;
    p->remain = p->stride;
    p->last_scheduled = 0;
    p->last_interrupted = 0;
    p->runtime = 0;
  }

  release(&ptable.lock);
  cprintf("userinit() released ptable.lock\n");
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
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  cprintf("PID: %d forked new PID: %d\n", curproc->pid, np->pid);

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
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

  if (stride_scheduler) {
    cprintf("adding stride details for PID: %d in fork()\n", pid);
    np->tickets = TICKETS_INIT;
    np->stride = STRIDE1/np->tickets;
    np->pass = 0;
    np->remain = np->stride;
    np->last_scheduled = 0;
    np->last_interrupted = 0;
    np->runtime = 0;
  }

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
  cprintf("exit() called form PID: %d\n", curproc->pid);
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
  cprintf("PID: %d, name: %s acquired ptable lock in wait()\n", curproc->pid, curproc->name);
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
        cprintf("PID: %d, name: %s released ptable lock in wait() 1\n", curproc->pid, curproc->name);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      cprintf("PID: %d, name: %s released ptable lock in wait() 2\n", curproc->pid, curproc->name);
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    cprintf("PID: %d, name: %s in wait() calling sleep() with lock held\n", curproc->pid, curproc->name);
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int
getticks(void)
{
  uint xticks;

  // cprintf("getting tickslock\n");
  acquire(&tickslock);
  // cprintf("got tickslock\n");
  xticks = ticks;
  // cprintf("releasing tickslock\n");
  release(&tickslock);
  // cprintf("released ticklock\n");
  return xticks;
  // return 0;
}

void
sched_RR(void)
{

  cprintf("running RR scheduler\n");
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;) {
    // Enable interrupts on this processor.
    // cprintf("enbaling interrupts\n");
    // sti();

    // Loop over process table looking for process to run.
    cprintf("trying to acquire lock\n");
    acquire(&ptable.lock);
    cprintf("acquired ptable lock\n");
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      cprintf("chosen process PID: %d\n", p->pid);
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      
      cprintf("switching to PID: %d\n",p->pid);
      swtch(&(c->scheduler), p->context);
      cprintf("got back from PID: %d\n",p->pid);
      switchkvm();
      cprintf("swicthedkvm() done\n");
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
    cprintf("released lock\n");
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
    h->size++;
    return 0;
}

struct proc* getmin(minheap* h) {
    return &h->arr[0];
}

// ----------------------STRIDE SCHEDULER HELPERS END -----------------------------

int entry_flag = 0;
char* entering = "entering swtch()";
char* exiting = "exiting swtch()";

void
sched_stride(void)
{

  cprintf("Running stride scheduler\n");
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;) {
    cprintf("---------SCHEDULER LOOP -------------\n");
    // Enable interrupts on this processor.
    // sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    cprintf("acquired lock\n");

    // for (int k = 0; k < NPROC; k++) {
    minheap heap;
    heap.size = 0;

    // cprintf("starting loop\n");
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      // cprintf("checking process: %d\n", p->pid);
      if(p->state == RUNNABLE) {
        cprintf("adding PID: %d pass: %d rtime: %d to heap\n", p->pid, p->pass, p->runtime);
        push(&heap, p);
      }
    }

    if (heap.size > 0) {
      struct proc* minproc = getmin(&heap);
      p = minproc;
      // p = &ptable.proc[0];
      cprintf("chosen process PID: %d, name: %s\n", p->pid, p->name);
      
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);

      // acquire(&ptable.lock);
      // cprintf("acquired lock\n");
      p->state = RUNNING;

      // cprintf("scheduler switching to RUNNABLE process: %s at %d ticks\n", p->name, getticks());
      p->last_scheduled = getticks();
      
      cprintf("switching to PID: %d, entry_flag = %d\n", p->pid, entry_flag);
      // release(&ptable.lock);
      swtch(&(c->scheduler), p->context);
      // acquire(&ptable.lock);
      cprintf("got back from PID: %d, entry_flag = %d\n", p->pid, entry_flag);
      cprintf("is lock held? - %d\n", holding(&ptable.lock));

      p->last_interrupted = getticks();
      p->runtime += p->last_interrupted - p->last_scheduled;

      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      // cprintf("releasing lock\n");
      // release(&ptable.lock);
    }
    // }
    cprintf("releasing lock\n");
    release(&ptable.lock);
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
    cprintf("scheduler()\n");
    if (stride_scheduler)
      sched_stride();
    else
      sched_RR();
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
  cprintf("sched() called from PID: %d\n", p->pid);

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;

  cprintf("PID: %d switching back to scheduler at %d ticks\n", p->pid, getticks());
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  cprintf("PID: %d yielding\n", myproc()->pid);
  acquire(&ptable.lock);  //DOC: yieldlock
  cprintf("made PID: %d changed %s -> RUNNABLE\n", myproc()->pid, myproc()->state);
  myproc()->state = RUNNABLE;
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
  cprintf("hello new process PID: %d, name: %s released scheduler lock in forkret()\n", myproc()->pid, myproc()->name);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    cprintf("calling iinit() from forkret()\n");
    iinit(ROOTDEV);
    cprintf("calling initlog() from forkret()\n");
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
  cprintf("PId: %d, name: %s called spleep()\n", p->pid, p->name);
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
    cprintf("sleep() previously has lock: %s\n", lk->name);
    acquire(&ptable.lock);  //DOC: sleeplock1
    cprintf("sleep() acquired ptable lock\n");
    release(lk);
    cprintf("sleep() released lock: %s\n", lk->name);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  cprintf("sleep() called sched with ptable lock held\n");
  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    cprintf("sleep() resetting its locks as before sched\n");
    release(&ptable.lock);
    acquire(lk);
    cprintf("resetting succesful\n");
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  cprintf("PID: %d, name: %s, wakeup() acquired lock\n", myproc()->pid, myproc()->name);
  wakeup1(chan);
  release(&ptable.lock);
  cprintf("PID: %d, name: %s, wakeup() released lock\n", myproc()->pid, myproc()->name);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;
  acquire(&ptable.lock);
  cprintf("kill() acquired lock\n");
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      cprintf("kill() released lock PID: %d, name: %s\n", p->pid, p->name);
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  cprintf("kill() released lock\n");
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
