#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

void syscall_init (void);
void syscall_exit (void);

#define NOT_LOADED 0
#define LOAD_SUCCESS 1
#define LOAD_FAIL 2

struct child_process {
  int pid;
  int load;
  bool wait;
  bool exit;
  int status;
  struct lock wait_lock;
  struct list_elem elem;
};

struct child_process* add_child_process (int pid);
struct child_process* get_child_process (int pid);
void remove_child_process (struct child_process *cp);
void remove_child_processes (void);

#endif /* userprog/syscall.h */
