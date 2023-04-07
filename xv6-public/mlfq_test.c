#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_LOOP 100
#define NUM_YIELD 200
#define NUM_SLEEP 500

#define NUM_THREAD 10
#define MAX_LEVEL 3

// #define TEST1
#define TEST2
// #define TEST3
// #define TEST4
// #define TEST5
// #define TEST6

int parent;

int fork_children(int time)
{
  int i, p;
  for (i = 0; i < NUM_THREAD; i++)
    if ((p = fork()) == 0)
    {
      sleep(time);
      return getpid();
    }
  return parent;
}


int fork_children2()
{
  int i, p;
  for (i = 0; i < NUM_THREAD; i++)
  {
    if ((p = fork()) == 0)
    {
      yield();
      return getpid();
    }
    else
    {
      setLevel(p, 2);
      setPriority(p, p % 4);
      printf(1, "setting pid %d priority %d\n", p, p % 4);
    }
  }
  return parent;
}

int max_level;

int fork_children3()
{
  int i, p;
  for (i = 0; i < NUM_THREAD; i++)
  {
    if ((p = fork()) == 0)
    {
      sleep(10);
      max_level = i;
      return getpid();
    }
  }
  return parent;
}
void exit_children()
{
  if (getpid() != parent)
    exit();
  while (wait() != -1);
}

int main(int argc, char *argv[])
{
  int i, pid;
  int count[MAX_LEVEL] = {0};

  parent = getpid();

  printf(1, "MLFQ test start\n");

#ifdef TEST1
  printf(1, "[Test 1] default\n");

  pid = fork_children(1);

  if (pid != parent)
  {
    for (i = 0; i < NUM_LOOP; i++)
    {
      sleep(1);
      int x = getLevel();
      if (x < 0 || x > 2)
      {
        printf(1, "Wrong level: %d\n", x);
        exit();
      }
      count[x]++;
    }
    printf(1, "Process %d\n", pid);
    for (i = 0; i < MAX_LEVEL; i++)
      printf(1, "L%d: %d\n", i, count[i]);
  }
  exit_children();


  pid = fork_children(5);

  if (pid != parent)
  {
    for (i = 0; i < NUM_LOOP; i++)
    {
      int x = getLevel();
      if (x < 0 || x > 2)
      {
        printf(1, "Wrong level: %d\n", x);
        exit();
      }
      count[x]++;
    }
    printf(1, "Process %d\n", pid);
    for (i = 0; i < MAX_LEVEL; i++)
      printf(1, "L%d: %d\n", i, count[i]);
  }
  exit_children();



  pid = fork_children(20);

  if (pid != parent)
  {
    for (i = 0; i < NUM_LOOP; i++)
    {
      int x = getLevel();
      if (x < 0 || x > 2)
      {
        printf(1, "Wrong level: %d\n", x);
        exit();
      }
      count[x]++;
    }
    printf(1, "Process %d\n", pid);
    for (i = 0; i < MAX_LEVEL; i++)
      printf(1, "L%d: %d\n", i, count[i]);
  }
  exit_children();

  printf(1, "[Test 1] finished\n");

#endif
#ifdef TEST2

  printf(1, "[Test 2] priorities w/o priority boosting\n");
  pid = fork_children2();

  if (pid != parent)
  {
    for (i = 0; i < NUM_LOOP; i++)
    {
      int x = getLevel();
      count[x]++;
    }
    printf(1, "Process %d\n", pid);
    for (i = 0; i < MAX_LEVEL; i++)
      printf(1, "L%d: %d\n", i, count[i]);
  }
  exit_children();
  printf(1, "[Test 2] finished\n");

#endif
#ifdef TEST3
  
  printf(1, "[Test 3] yield\n");
  pid = fork_children2();

  if (pid != parent)
  {
    for (i = 0; i < NUM_YIELD; i++)
    {
      int x = getLevel();
      if (x < 0 || x > 4)
      {
        printf(1, "Wrong level: %d\n", x);
        exit();
      }
      count[x]++;
      yield();
    }
    printf(1, "Process %d\n", pid);
    for (i = 0; i < MAX_LEVEL; i++)
      printf(1, "L%d: %d\n", i, count[i]);
  }
  exit_children();
  printf(1, "[Test 3] finished\n");

#endif
#ifdef TEST4

  printf(1, "[Test 4] sleep\n");
  pid = fork_children2();

  if (pid != parent)
  {
    for (i = 0; i < NUM_SLEEP; i++)
    {
      int x = getLevel();
      if (x < 0 || x > 4)
      {
        printf(1, "Wrong level: %d\n", x);
        exit();
      }
      count[x]++;
      sleep(1);
    }
    printf(1, "Process %d\n", pid);
    for (i = 0; i < MAX_LEVEL; i++)
      printf(1, "L%d: %d\n", i, count[i]);
  }
  exit_children();
  printf(1, "[Test 4] finished\n");
#endif
#ifdef TEST5  
  printf(1, "[Test 5] max level\n");
  pid = fork_children3();

  if (pid != parent)
  {
    for (i = 0; i < NUM_LOOP; i++)
    {
      int x = getLevel();
      if (x < 0 || x > 4)
      {
        printf(1, "Wrong level: %d\n", x);
        exit();
      }
      count[x]++;
      if (x > max_level)
        yield();
    }
    printf(1, "Process %d\n", pid);
    for (i = 0; i < MAX_LEVEL; i++)
      printf(1, "L%d: %d\n", i, count[i]);
  }
  exit_children();
  printf(1, "[Test 5] finished\n");
#endif
#ifdef TEST6  
  printf(1, "[Test 6] setPriority return value\n");
  int child = fork();

  if (child == 0)
  {
    int grandson;
    sleep(10);
    grandson = fork();
    if (grandson == 0)
    {
      // r = setPriority(getpid() - 2, 0);
      // if (r != -1)
      //   printf(1, "wrong: setPriority of parent: expected -1, got %d\n", r);
      // r = setPriority(getpid() - 3, 0);
      // if (r != -1)
      //   printf(1, "wrong: setPriority of ancestor: expected -1, got %d\n", r);
    }
    else
    {
      setPriority(grandson, 0);
      // if (r != 0)
      //   printf(1, "wrong: setPriority of child: expected 0, got %d\n", r);
      // r = setPriority(getpid() + 1, 0);
      // if (r != -1)
      //   printf(1, "wrong: setPriority of other: expected -1, got %d\n", r);
    }
    sleep(20);
    wait();
  }
  else
  {
    int child2 = fork();
    sleep(20);
    if (child2 == 0)
      sleep(10);
    else
    {
      // r = setPriority(child, -1);
      // if (r != -2)
      //   printf(1, "wrong: setPriority out of range: expected -2, got %d\n", r);
      // r = setPriority(child, 11);
      // if (r != -2)
      //   printf(1, "wrong: setpriority out of range: expected -2, got %d\n", r);
      setPriority(child, 10);
      // if (r != 0)
        // printf(1, "wrong: setPriority of child: expected 0, got %d\n", r);
       setPriority(child + 1, 10);
      // if (r != 0)
        // printf(1, "wrong: setPriority of child: expected 0, got %d\n", r);
      // r = setPriority(child + 2, 10);
      // if (r != -1)
      //   printf(1, "wrong: setPriority of grandson: expected -1, got %d\n", r);
      // r = setPriority(parent, 5);
      // if (r != -1)
      //   printf(1, "wrong: setPriority of self: expected -1, got %d\n", r);
    }
  }

  exit_children();
  printf(1, "done\n");
  printf(1, "[Test 6] finished\n");
#endif
  exit();
}

