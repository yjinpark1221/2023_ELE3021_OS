#include "types.h"
#include "stat.h"
#include "user.h"

#define BUFFER_SIZE 128

char* strtok(char* buffer, char c) {
  static char* prev;
  if (buffer == 0) {
    buffer = prev;
  }
  for (char* tmp = buffer; *tmp; ++tmp) {
    if (*tmp == c) {
      *tmp = 0;
      prev = tmp + 1;
      break;
    }
  }
  return buffer;
}

int strncmp(char* s1, char* s2, int cnt) {
  for (int i = 0; i < cnt; ++i) {
    if (s1[i] != s2[i]) return 1;
  }
  return 0;
}

int
main(int argc, char *argv[])
{
  enum itype { LIST, KILL, EXECUTE, MEMLIM, EXIT, itypeCount};
  char* instructions[] = {
    [LIST]    "list",
    [KILL]    "kill",
    [EXECUTE] "execute",
    [MEMLIM]  "memlim",
    [EXIT]    "exit"
  };
  char buffer[BUFFER_SIZE];
  for (; ;) {
    int curIns = -1;
    gets(buffer, BUFFER_SIZE);
    for (int i = 0; i < BUFFER_SIZE; ++i) {
      if (buffer[i] == '\n') {
        buffer[i] = 0;
        break;
      }
    }
    char* token = strtok(buffer, ' ');
    for (int i = 0; i < itypeCount; ++i) {
      if (strncmp(instructions[i], token, sizeof instructions[i]) == 0) {
        curIns = i;
        break;
      }
    }
    if (curIns == -1) {
      continue;
    }
    else if (curIns == LIST) {
      printProcList();
    }
    else if (curIns == KILL) {
      int pid;
      token = strtok(0, ' ');
      pid = atoi(token);
      if (kill(pid)) {
        printf(1, "kill failure\n");
      }
      else {
        printf(1, "kill success\n");
      }
    }
    else if (curIns == EXECUTE) {
      char* path = strtok(0, ' ');
      token = strtok(0, ' ');
      int stacksize = atoi(token);
      int pid = fork();

      char* argv[] = { path, 0 };
      if (pid == 0) {
        if (exec2(path, argv, stacksize)) {
          printf(1, "execute failure\n");
          exit();
        }
        exit();
      }
    }
    else if (curIns == MEMLIM) {
      int pid, limit;
      token = strtok(0, ' ');
      pid = atoi(token);
      token = strtok(0, ' ');
      limit = atoi(token);
      if (setmemorylimit(pid, limit)) {
        printf(1, "memlim failure\n");
      }
      else {
        printf(1, "memlim success\n");
      }
    }
    else if (curIns == EXIT) {
      exit();
    }
  }
}
