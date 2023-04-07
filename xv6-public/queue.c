// project1 scheduler

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

void printqueue(struct queue *q)
{
  cprintf("front %d / back %d / size %d\n", q->front ? q->front->pid : -1, q->back ? q->back->pid : -1, q->size);
  for (struct proc *p = q->front; p; p = p->next)
  {
    cprintf("pid = %d\t pname = %s\t state = %d\n", p->pid, p->name, p->state);
  }
  cprintf("\n");
}

void erasequeue(struct queue *q, struct proc *p)
{
  if (p->queue != q) {
    panic("erasequeue:");
  }
  if (q == NULL || p == NULL)
    return;
  bool found = 0;
  for (struct proc *pp = frontqueue(q); pp; pp = pp->next)
  {
    if (pp == p)
    {
      found = 1;
      break;
    }
  }
  if (!found)
  {
    procdump();
    cprintf("pid = %d\n", p->pid);
    panic("not in queue\n");
  }
  p->queue = NULL;
  --q->size;
  if (q->front == q->back)
  {
    q->front = q->back = NULL;

    p->prev = p->next = NULL;
    return;
  }
  if (q->front == p)
  {
    q->front = p->next;
    q->front->prev = NULL;
    p->prev = p->next = NULL;
    return;
  }
  if (q->back == p)
  {
    q->back = p->prev;
    q->back->next = NULL;
    p->prev = p->next = NULL;
    return;
  }
  if (p->prev == NULL || p->next == NULL)
  {
    procdump();
    cprintf("pid %d, level %d,prevnull %d, nextnull %d\n", p->pid, p->level, p->prev == NULL, p->next == NULL);
    panic("should have prev and next\n");
  }
  p->prev->next = p->next;
  p->next->prev = p->prev;
  p->prev = p->next = NULL;
}

void pushqueue(struct queue *q, struct proc *n)
{
  if (q == NULL || n == NULL)
    return;
  if (n->queue) {
    panic("pushqueue");
    return;
  }
  n->queue = q;
  ++q->size;
  n->prev = q->back;
  if (q->back == NULL)
  {
    q->front = q->back = n;
    n->prev = n->next = NULL;
    return;
  }
  q->back->next = n;
  n->next = NULL;
  q->back = n;
}

struct proc *popqueue(struct queue *q)
{
  struct proc *ret = q->front;
  if (ret == NULL)
  {
    return ret;
  }
  ret->queue = NULL;
  --q->size;
  if (q->front == q->back)
  {
    q->front = q->back = NULL;
    ret->prev = ret->next = NULL;
    return ret;
  }
  q->front = q->front->next;
  q->front->prev = NULL;
  ret->next = ret->prev = NULL;
  return ret;
}

struct proc *frontqueue(struct queue *q)
{
  return q->front;
}

void pushfrontqueue(struct queue *q, struct proc *p)
{
  if (q == NULL || p == NULL) return;
  if (p->queue) {
    panic("pushfrontqueue");
    return;
  }

  p->queue = q;
  if (q->front == NULL)
  {
    q->front = q->back = p;
    p->prev = p->next = NULL;
    return;
  }
  ++q->size;
  if (q->front == q->back)
  {
    q->front = p;
    p->next = q->back;
    q->back->prev = p;
    p->prev = NULL;
    q->back->next = NULL;
    return;
  }
  p->prev = NULL;
  p->next = q->front;
  q->front = p;
  p->next->prev = p;
  return;
}
