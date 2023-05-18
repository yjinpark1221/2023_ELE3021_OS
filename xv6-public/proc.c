#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#define PRINTFL() cprintf("%s %d\n", __FUNCTION__, __LINE__)
#define NEXTTH(i) (((i) + 1) % NTHREAD)
struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
int nexttid = 1;
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
  int thidx;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) {
      thidx = allocth(p);
      goto found;
    }
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->thread[thidx].state = EMBRYO;
  p->pid = nextpid++;
  p->limit = 0;
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    p->thread[thidx].state = UNUSED;
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
  p->thread[p->thidx].state = RUNNABLE;
  copyprocth(p, p->thidx);
  
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
  uint newsz = sz + n;
  if (curproc->limit && sz + n > curproc->limit) {
    cprintf("[WARN] in growproc function, trying to exceed limit. growing till limit");
    newsz = curproc->limit;
  }
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, newsz)) == 0)
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

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    np->thread[np->thidx].state = UNUSED;
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

  np->limit = curproc->limit;
  np->stacksize = curproc->stacksize;

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  np->thread[np->thidx].state = RUNNABLE;
  copyprocth(np, np->thidx);

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

  curproc->thread[curproc->thidx].state = ZOMBIE;
  int shouldexit = allthzombie(curproc);
  
  if (shouldexit) {
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
    curproc->state = ZOMBIE;
  }

  // Jump into the scheduler, never to return if shouldexit is true.
  sched();
  if (p->state == ZOMBIE) panic("zombie exit");
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
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        p->limit = 0;
        for (int i = 0; i < NTHREAD; ++i) {
          struct thread* th = p->thread + i;
          if (th->state == UNUSED) continue;
          th->tid = 0;
          kfree(th->kstack);
          th->kstack = 0;
          th->state = UNUSED;
          th->tf = 0;
          th->context = 0;
          th->chan = 0;
          th->proc = 0;
        }
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
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE || !existrunnable(p))
        continue;
      int thidx = thidxtorun(p);
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      copythproc(p, thidx);
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      p->thread[thidx].state = RUNNING;
      swtch(&(c->scheduler), p->context);
      switchkvm();

      copyprocth(p, thidx);

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
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
  copythproc(p, p->thidx);
  swtch(&p->context, mycpu()->scheduler);
  copyprocth(p, p->thidx);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  myproc()->thread[myproc()->thidx].state = RUNNABLE;
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
  struct thread* th = p->thread + p->thidx;
  th->chan = chan;
  th->state = SLEEPING;
  p->state = RUNNABLE;

  sched();

  // Tidy up.
  th->chan = 0;

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
  struct thread* th;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    for (int i = 0; i < NTHREAD; ++i) {
      if (p->state != RUNNABLE) continue;
      th = p->thread + i;
      if(th->state == SLEEPING && th->chan == chan)
        th->state = RUNNABLE;
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
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      struct thread* th = p->thread + p->thidx;
      // Wake process from sleep if necessary.
      if(th->state == SLEEPING)
        th->state = RUNNABLE;
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
    cprintf("%d %s %s %p %p %p", p->pid, state, p->name, p->kstack, p->tf, p->context);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
    for (int i = 0; i < NTHREAD; ++i) {
      struct thread* th = p->thread + i;
      state = states[th->state];
      if (th->state != UNUSED) cprintf("- %d %d %s %p %p %p\n", i, th->tid, state, th->kstack, th->tf, th->context);
    }
  }
}

struct proc *getProc(int pid)
{
  acquire(&ptable.lock);
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; ++p)
  {
    if (p->pid == pid && p->state != UNUSED)
    {
      release(&ptable.lock);
      return p;
    }
  }
  release(&ptable.lock);
  return 0;
}

int setmemorylimit(int pid, int limit) {
  struct proc* p = getProc(pid);
  if (p == 0) {
    return -1;
  }
  if (limit < 0) {
    return -1;
  }
  if (p->sz > limit) {
    return -1;
  }
  p->limit = limit;
  return 0;
}

void printProc(struct proc* p) {
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  cprintf("[ %d ]\t%s\t%d\t%d\t%d\t%d\t%s\n", p->pid, (p->state < 6 && p->state >= 0) ? states[p->state] : "???", existrunnable(p), p->stacksize, p->sz, p->limit, p->name);
}

int printProcList() {
  struct proc* p;
  acquire(&ptable.lock);
  cprintf("[pid]\t[state]\t[runth]\t[stack]\t[size]\t[limit]\t[name]\n");
  cprintf("--------------------------------------------------\n");
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->state != UNUSED) {
      printProc(p);
    }
  }
  release(&ptable.lock);
  cprintf("\n");
  return 0;
}

int thidxtorun(struct proc* p) {
  int previdx = p->thidx;
  for (int i = NEXTTH(previdx); i != previdx; i = NEXTTH(i)) {
    struct thread* th = p->thread + i;
    if (th->state == RUNNABLE) return i;
  }
  return 0;
}

int copythproc(struct proc* p, int thidx) {
  struct thread* th = p->thread + thidx;
  if (th->proc != p) panic("thread proc inconsistent");
  p->kstack = th->kstack;
  p->tf = th->tf;
  p->context = th->context;
  p->chan = th->chan;
  p->thidx = thidx;
  return 0;
}

int existrunnable(struct proc* p) {
  for (int i = 0; i < NTHREAD; ++i) {
    if (p->thread[i].state == RUNNABLE) return 1;
  }
  return 0;
}

int copyprocth(struct proc* p, int thidx) {
  struct thread* th = p->thread + thidx;
  th->kstack = p->kstack;
  th->tf = p->tf;
  th->context = p->context;
  th->chan = p->chan;
  return 0;
}

int allocth(struct proc* p) {
  for (int i = 0; i < NTHREAD; ++i) {
    struct thread* th = p->thread + i;
    if (th->state == UNUSED) {
      th->proc = p;
      th->tid = nexttid++;
      th->state = EMBRYO;
      return i;
    }
  }
  return -1;
}

int allthzombie(struct proc* p) {
  for (int i = 0; i < NTHREAD; ++i) {
    struct thread* th = p->thread + i;
    if (th->state != UNUSED && th->state != ZOMBIE) {
      return 0;
    }
  }
  return 1;
}

int thread_create(thread_t* thread, void*(*start_routine)(void*), void* arg) {
  char* sp;
  struct proc *curproc = myproc();
  int thidx;         // new thread index
  struct thread* nt; // new thread pointer
  uint sz, ustack[3+MAXARG+1];

// allocproc

  acquire(&ptable.lock);

  // Allocate thread.
  if ((thidx = allocth(curproc)) == -1) {
    return -1;
  }
  nt = curproc->thread + thidx;

  // Allocate kernel stack.
  if((nt->kstack = kalloc()) == 0){
    nt->state = UNUSED;
    return -1;
  }
  sp = nt->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *nt->tf;
  nt->tf = (struct trapframe*)sp;
  *nt->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  nt->tf->eax = 0; // not necessary

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *nt->context;
  nt->context = (struct context*)sp;
  memset(nt->context, 0, sizeof *nt->context);
  nt->context->eip = (uint)forkret;

// exec

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(curproc->sz);
  if((sz = allocuvm(curproc->pgdir, sz, sz + (1 + curproc->stacksize) * PGSIZE)) == 0)
    goto bad;
  clearpteu(curproc->pgdir, (char*)(sz - (1 + curproc->stacksize) * PGSIZE));
  sp = (char*) sz;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = (uint)arg;

  sp -= 2 * 4;
  if(copyout(curproc->pgdir, (uint)sp, ustack, 2*4) < 0)
    goto bad;

  curproc->sz = sz;
  switchuvm(curproc); // not necessary
  nt->tf->eip = (uint)start_routine;
  nt->tf->esp = (uint)sp;
  nt->state = RUNNABLE;
  release(&ptable.lock);
  *thread = nt->tid;
  PRINTFL();
  return 0;

 bad:
  if(curproc->pgdir)
    freevm(curproc->pgdir);
  return -1;
}

// void thread_exit(void* retval) {

// }
// int thread_join(thread_t thread, void** retval) {

// }
