#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
// #define DEBUG

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
  struct queue queue[NQUEUE];
  int lockpid;
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void pinit(void)
{
  initlock(&ptable.lock, "ptable");
  for (int i = 0; i < NQUEUE; ++i) {
    ptable.queue[i].level = i;
  }
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

// PAGEBREAK: 32
//  Look in the process table for an UNUSED proc.
//  If found, change state to EMBRYO and initialize
//  state required to run in the kernel.
//  Otherwise return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  // project1 scheduler
  // initialize priority, prev, next, queue of the process
  // push the process into L0 queue
  p->priority = 3;
  p->next = p->prev = NULL;
  p->tq = 0;
  pushqueue(ptable.queue, p);

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

// PAGEBREAK: 32
//  Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
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
  p->tf->eip = 0; // beginning of initcode.S

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
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
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

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  cprintf("\nforked\n");
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;


  acquire(&ptable.lock);

  // schedulerUnlock manually
  if (ptable.lockpid == curproc->pid) {
    release(&ptable.lock);
    // lockpid does not change between release and schedulerLock
    schedulerUnlock(PASSWORD);
    acquire(&ptable.lock);
  }
  
  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
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
int wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
        // Found one.

        // project1 scheduler 
        // erase process from the queue
        erasequeue(p->queue, p);
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        // clear priority, tq
        p->priority = p->tq = -1;

        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); // DOC: wait-sleep
  }
}

// project1 scheduler
// PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

    // choose a process to run and check if the process has time quantum left
    acquire(&ptable.lock);
    for (;;)
    {
      p = getProcessToRun();
      if (p == NULL)
      {
        break;
      }
      if (p->tq == getTimeQuantum(p->level))
      {
        expireTimeQuantum(p);
      }
      else
        break;
    }

    if (p == NULL)
    {
      release(&ptable.lock);
      continue;
    }

    // mark that the process used one tick
    ++p->tq;

    // for ROUND ROBIN in L0, L1
    // move the process to back of the queue
    if (p->level < NQUEUE - 1) {
      struct queue* tmp = p->queue;
      erasequeue(p->queue, p);
      pushqueue(tmp, p);
    }

    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.

#ifdef DEBUG
    cprintf(" [ %d, L%d ( %d ), tq %d / %d ]  running\n", p->pid, p->level, p->priority, p->tq, getTimeQuantum(p->level));
#endif
    cprintf("\tpid %d\tlevel%d\t|", p->pid, p->level);
    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;
    swtch(&(c->scheduler), p->context);
    switchkvm();

    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;

    acquire(&tickslock);
    uint xticks = ticks;
    release(&tickslock);
    if (xticks >= 100) boostPriority();
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  acquire(&ptable.lock); // DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
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
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        // DOC: sleeplock0
    acquire(&ptable.lock); // DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { // DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// PAGEBREAK!
//  Wake up all processes sleeping on chan.
//  The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// PAGEBREAK: 36
//  Print a process listing to console.  For debugging.
//  Runs when user types ^P on console.
//  No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [EMBRYO] "embryo",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s L%d, tq %d / %d, priority %d", p->pid, state, p->name, p->level, p->tq, getTimeQuantum(p->level), p->priority);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// functions for 
// project1 scheduler

// calculates time quantum for each level
int getTimeQuantum(int i)
{
  return 2 * i + 4;
}

// when called, ptable lock must be acquired
// return proc pointer of that pid
struct proc *getProc(int pid)
{
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; ++p)
  {
    if (p->pid == pid && p->state != UNUSED)
    {
      return p;
    }
  }
  return NULL;
}

// when called, ptable lock must be acquired
// called in boostPriority function for every process
// clears priority, tq
// move to L0 queue
void clearProc(struct proc *p)
{
  if (p == NULL || p->state == UNUSED)
    return;
  p->priority = 3;
  p->tq = 0;
  erasequeue(p->queue, p);
  pushqueue(ptable.queue, p);
}

// when called, ptable lock must be acquired
// called in scheduler() every 100 ticks ()
// if scheduler is locked, unlock scheduler
// move every process to L0 queue by calling clearProc function
void boostPriority()
{
// #ifdef DEBUG
  cprintf("[[[ boosting ]]]\n");
// #endif
  acquire(&tickslock);
  ticks = 0;
  release(&tickslock);

  if (ptable.lockpid) {
    release(&ptable.lock);
    // lockpid does not change between release and schedulerLock
    schedulerUnlock(PASSWORD);
    acquire(&ptable.lock);
  }

  for (int i = 0; i < NPROC; ++i)
  {
    for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; ++p)
    {
      if (p->state != UNUSED)
      {
        clearProc(p);
      }
    }
  }
}

// when called, ptable lock must be acquired
// called in scheduler function
// returns a process to run (checking time quantum is not included)
struct proc *getProcessToRun()
{
  if (ptable.lockpid) {
    struct proc* p = getProc(ptable.lockpid);
    if (p->state == RUNNABLE) return p;
    else {
      // unlock and get runnable process
      cprintf("[WARN] Scheduler locked by process that is NOT RUNNABLE\n\tUnlocking scheduler for CPU utilization\n");
      release(&ptable.lock);
      schedulerUnlock(PASSWORD);
      acquire(&ptable.lock);
    }
  }
  struct proc *p = NULL;
  for (;;)
  {
    for (int i = 0; i < NQUEUE - 1; ++i)
    {
      for (p = frontqueue(ptable.queue + i); p; p = p->next)
      {
        if (p->state != RUNNABLE)
          continue;
        return p;
      }
    }

    int minPriority = 4;
    for (struct proc *tmp = frontqueue(ptable.queue + NQUEUE - 1); tmp; tmp = tmp->next)
    {
      if (tmp->state != RUNNABLE)
        continue;
      if (tmp->priority < minPriority)
      {
        minPriority = tmp->priority;
        p = tmp;
      }
    }
    return p;
  }
}

// when called, ptable lock must be acquired
// moves to lower queue or change priority
void expireTimeQuantum(struct proc *p)
{
#ifdef DEBUG
  cprintf("[[[ %d tq expired ]]]\n", p->pid);
  procdump();
#endif
  p->tq = 0;
  if (p->level < NQUEUE - 1)
  {
    struct queue* tmpq = p->queue;
    erasequeue(p->queue, p);
    pushqueue(tmpq + 1, p);
  }
  else
  {
    if (p->priority == 0)
      return;
    --p->priority;
  }
}

// when called, ptable lock must be acquired
// for debugging, print all queues for debugging
void printqueues()
{
  cprintf("//\n");
  printqueue(ptable.queue);
  printqueue(ptable.queue + 1);
  printqueue(ptable.queue + 2);
  cprintf("//\n");
}

// system calls
// returns queue level
int getLevel()
{
  struct proc *p = myproc();
  if (p == 0)
    return -1;

  int level = p->level;
  if (level < 0 || level >= NQUEUE)
    return -1;

  return level;
}

// sets priority
void setPriority(int pid, int priority)
{
  if (priority < 0 || priority > 3) {
    cprintf("[WARN] Invalid priority\n\tPlease use 0, 1, 2, 3 as priority.\n");
    return;
  }
  acquire(&ptable.lock);

  struct proc *p = getProc(pid);
  if (p)
    p->priority = priority;
  else {
    cprintf("[WARN] Invalid pid\n\tPlease use ");
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->state == RUNNABLE || p->state == RUNNING) {
        cprintf("%d ", p->pid);
      }
    }
    cprintf("as pid.\n");
  }
  release(&ptable.lock);
}

// locks scheduler
// scheduler can only run current process
void schedulerLock(int password)
{

  acquire(&ptable.lock);

  struct proc *p = myproc();
  if (password != PASSWORD)
  {
    cprintf("[ERROR] Wrong password\npid : %d\ntime quantum : %d\nlevel : %d\n", p->pid, p->tq, p->level);
    p->killed = 1;

    release(&ptable.lock);
    return;
  }
  
  if (ptable.lockpid == p->pid) {
    cprintf("[WARN] Imprudent locking\n\tThis can affect performance of other processes\n");
  }
  else if (ptable.lockpid) {
    // this never happens because other processes cannot get CPU from scheduler(single core)
    cprintf("[ERROR] Already Locked by another process\n");
    p->killed = 1;

    release(&ptable.lock);
    return;
  }

#ifdef DEBUG
  cprintf("[[[ locking scheduler ]]]\n");
#endif
  ptable.lockpid = p->pid;

  release(&ptable.lock);

  acquire(&tickslock);
  ticks = 0;
  release(&tickslock);

  return;
}

// unlocks scheduler
// if wrong password, print error message and exit
// if password is correct and called by process that has locked the scheduler,
// back to mlfq, current process to L0 front
void schedulerUnlock(int password)
{
  struct proc *p = myproc();
  acquire(&ptable.lock);

  if (password != PASSWORD)
  {
    cprintf("[ERROR] Wrong password\n\tpid : %d\n\ttime quantum : %d\n\tlevel : %d\n", p ? p->pid : -1, p ? p->tq : -1, p ? p->level : -1);
    p->killed = 1;

    release(&ptable.lock);
    return;
  }


  if (ptable.lockpid == 0)
  {
    cprintf("[WARN] Trying to unlock scheduler (currently not locked)\n");

    release(&ptable.lock);
    return;
  }

  if (p && p->pid != ptable.lockpid)
  {
    cprintf("[WARN] Trying to unlock scheduler (not locked by this process)\n");
    cprintf("\tCurrent lockpid : %d, tried locking by %d\n", ptable.lockpid, p->pid);

    release(&ptable.lock);
    return;
  }

  if (p) {
    erasequeue(p->queue, p);
    pushfrontqueue(ptable.queue, p);

    p->priority = 3;
    p->tq = 0;
  }

#ifdef DEBUG
  cprintf("[[[ unlocking scheduler ]]]\n");
#endif

  ptable.lockpid = 0;

  release(&ptable.lock);

  return;
}

// for debugging and test code
// moves to queue of that level
void setLevel(int pid, int level) {

  if (level < 0 || level > 3) {
    cprintf("[WARN] Invalid level\n");
    cprintf("\tPlease use 0, 1, 2 as level.\n");
    return;
  }

  acquire(&ptable.lock);

  struct proc *p = getProc(pid);
  if (p) {
    erasequeue(p->queue, p);
    pushqueue(ptable.queue + level, p);

  }
  else {
    cprintf("[WARN] Invalid pid\n\tPlease use ");
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->state == RUNNABLE || p->state == RUNNING) {
        cprintf("%d ", p->pid);
      }
    }
    cprintf("as pid.\n");
  }

  release(&ptable.lock);
  return;
}