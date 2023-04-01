# Default Scheduler Analysis

## General

## Function Flow

### `yield` function

#### When is it called?

default scheduler의 코드에서 한 곳에서만 호출된다. 

`trap.c`
```
  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();
```

#### What does it do?
`proc.c`
```
// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}
```
1. ptable.lock을 acquire한다. 
2. 실행중이던 프로세스의 상태를 RUNNABLE로 바꾼다. 
3. sched함수를 호출한다. 
4. ptable.lock을 release한다. 

### `sched` 함수
#### When is it called?
`exit`, `yield`, `sleep` 함수에서 호출된다. 
#### What does it do? 
`proc.c`
```
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
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}
```
1. ptable lock을 잡고있는지 확인한다.
2. ncli가 1인지 확인한다.
3. p->state가 RUNNING이 아닌지 확인한다. 
3. sched interrupt가 막혀있는지 확인한다. 
5. 이 커널 스레드의 intena(interrupt enable)를 저장한다. 
6. scheduler로 swtch함수를 호출한다. 
7. 저장했던 intena를 복구한다. 

### `swtch` 함수
#### When is it called?
`sched`함수에서는 
    
    swtch(&p->context, mycpu()->scheduler);

`scheduler`함수에서는

    swtch(&(c->scheduler), p->context);

#### What does it do?
`swtch.S`
```
# Context switch
#
#   void swtch(struct context **old, struct context *new);
# 
# Save the current registers on the stack, creating
# a struct context, and save its address in *old.
# Switch stacks to new and pop previously-saved registers.

.globl swtch
swtch:
  movl 4(%esp), %eax
  movl 8(%esp), %edx

  # Save old callee-saved registers
  pushl %ebp
  pushl %ebx
  pushl %esi
  pushl %edi

  # Switch stacks
  movl %esp, (%eax)
  movl %edx, %esp

  # Load new callee-saved registers
  popl %edi
  popl %esi
  popl %ebx
  popl %ebp
  ret
```
1. 아래 두 레지스터에 함수 인자를 저장한다. 

    - `%eax`: `struct context **old`

    - `%edx` :  `struct context *new`

2. 현재 스택에 범용 레지스터의 현재 값들을 push한 후, 이 스택의 top을 old에 넣음으로써, stack형태로 저장되어있는 현재 context의 주소를 저장한다. 

3. 스택 포인터 레지스터에 new를 넣음으로써 새로운 context를 스택으로 받아와 pop하면서 범용 레지스터로 로드해준다. 

4. `ret`을 하며 new context에 저장되어있던 p->scheduler의 명령어의 위치를 eip에 로드한다. 

### `scheduler` 함수
#### When is it called?

main함수에서 호출되어 무한루프를 돎. 

context switch를 통해 다른 프로세스와 scheduler 프로세스가 번갈아 실행됨.

#### What does it do?
`proc.c`
```

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
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}
```

for 문 안에서 
1. ptable의 lock을 acquire한다. 
2. Round Robin으로 실행할 프로세스를 선택한다. 
3. context switch한다. 
(프로세스가 ptable lock을 release하고 다시 scheduler로 context switch하기 전에 ptable의 lock을 acquire하고 state을 running이 아닌 것으로 바꿨을 것을 기대한다.)
5. ptable lock을 release한다. 