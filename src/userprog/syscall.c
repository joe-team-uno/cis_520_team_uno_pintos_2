#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
 
 
static int sys_halt (void);
static int sys_exit (int status);
static int sys_exec (const char *ufile);
static int sys_wait (tid_t);
static int sys_create (const char *ufile, unsigned initial_size);
static int sys_remove (const char *ufile);
static int sys_open (const char *ufile);
static int sys_filesize (int handle);
static int sys_read (int handle, void *udst_, unsigned size);
static int sys_write (int handle, void *usrc_, unsigned size);
static int sys_seek (int handle, unsigned position);
static int sys_tell (int handle);
static int sys_close (int handle);
 
static void syscall_handler (struct intr_frame *);
static void copy_in (void *, const void *, size_t);
 
/* Serializes file system operations. */
static struct lock fs_lock;
 
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&fs_lock);
}
 
/* System call handler. */

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  typedef int syscall_function(int, int, int);

  struct syscall
  {
    size_t arg_cnt;
    syscall_function *func;
  };

  static const struct syscall syscall_table[]=
  {
		{0, (syscall_function *) sys_halt},
    {1, (syscall_function *) sys_exit},
    {1, (syscall_function *) sys_exec},
		{1, (syscall_function *) sys_wait},
		{2, (syscall_function *) sys_create},
		{1, (syscall_function *) sys_remove},
    {1, (syscall_function *) sys_open},
		{1, (syscall_function *) sys_filesize},
		{3, (syscall_function *) sys_read},
		{3, (syscall_function *) sys_write},
		{2, (syscall_function *) sys_seek},
		{1, (syscall_function *) sys_tell},
		{1, (syscall_function *) sys_close}
  };
  
  const struct syscall *sc;
  unsigned call_nr;
  int args[3];
  copy_in (&call_nr, f->esp, sizeof call_nr);
  if (call_nr >= sizeof syscall_table / sizeof *syscall_table)
    thread_exit ();
  sc = syscall_table + call_nr;

  ASSERT (sc->arg_cnt <= sizeof args/ sizeof *args);
  memset (args, 0, sizeof args);
  copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * sc->arg_cnt);
  f->eax = sc->func (args[0], args[1], args[2]);
  
  
}

/* Returns true if UADDR is a valid, mapped user address,
   false otherwise. */
static bool
verify_user (const void *uaddr) 
{
  return (uaddr < PHYS_BASE
          && pagedir_get_page (thread_current ()->pagedir, uaddr) != NULL);
}
 
/* Copies a byte from user address USRC to kernel address DST.
   USRC must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static inline bool
get_user (uint8_t *dst, const uint8_t *usrc)
{
  int eax;
  asm ("movl $1f, %%eax; movb %2, %%al; movb %%al, %0; 1:"
       : "=m" (*dst), "=&a" (eax) : "m" (*usrc));
  return eax != 0;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static inline bool
put_user (uint8_t *udst, uint8_t byte)
{
  int eax;
  asm ("movl $1f, %%eax; movb %b2, %0; 1:"
       : "=m" (*udst), "=&a" (eax) : "q" (byte));
  return eax != 0;
}
 
/* Copies SIZE bytes from user address USRC to kernel address
   DST.
   Call thread_exit() if any of the user accesses are invalid. */
static void
copy_in (void *dst_, const void *usrc_, size_t size) 
{
	uint8_t *dst = dst_;
	const uint8_t *usrc = usrc_;

  for (; size > 0; size--, dst++, usrc++) 
		if (usrc >= (uint8_t *) PHYS_BASE || !get_user (dst, usrc)) 
      thread_exit ();
}
 
/* Creates a copy of user string US in kernel memory
   and returns it as a page that must be freed with
   palloc_free_page().
   Truncates the string at PGSIZE bytes in size.
   Call thread_exit() if any of the user accesses are invalid. */
static char *
copy_in_string (const char *us) 
{
  char *ks;
  size_t length;
 
  ks = palloc_get_page (0);
  if (ks == NULL)
  {
    thread_exit ();
  }
 
  for (length = 0; length < PGSIZE; length++)
    {
      if (us >= (char *) PHYS_BASE || !get_user ((uint8_t *)(ks + length), (uint8_t *) (us++))) 
      {
        palloc_free_page (ks);
        thread_exit (); 
      }
      if (ks[length] == '\0')
        return ks;
    }
  ks[PGSIZE - 1] = '\0';
  return ks;
}
 
/* Halt system call. */
static int
sys_halt (void)
{
  shutdown_power_off ();
}
 
/* Exit system call. */
static int
sys_exit (int exit_code) 
{
  thread_current ()->wait_status->exit_code = exit_code;
  thread_exit ();
  NOT_REACHED ();
}
 
/* Exec system call. */
static int
sys_exec (const char *ufile) 
{
  int ret = -1;
	if(!verify_user(ufile))
		return sys_exit(-1);
  lock_acquire (&fs_lock);
	ret = process_execute(ufile);
  lock_release (&fs_lock);
  return ret;
}
 
/* Wait system call. */
static int
sys_wait (tid_t child)
{
	if(child == (tid_t) NULL)
		return sys_exit(-1);
	return process_wait(child);
}
 
/* Create system call. */
static int
sys_create (const char *ufile, unsigned initial_size) 
{
  if (!verify_user(ufile))
    return sys_exit (-1);
  if(ufile != NULL)
    return filesys_create (ufile, (off_t) initial_size);
	return -1;
}
 
/* Remove system call. */
static int
sys_remove (const char *ufile) 
{
	if(!verify_user(ufile))
		return sys_exit(-1);

	return filesys_remove(ufile);
}
 
/* A file descriptor, for binding a file handle to a file. */
struct file_descriptor
{
  struct list_elem elem;      /* List element. */
  struct file *file;          /* File. */
  int handle;                 /* File handle. */
};
 
/* Open system call. */
static int
sys_open (const char *ufile) 
{
	if(!verify_user(ufile))
	{
		thread_exit();
		return -1;
	}
  char *kfile = copy_in_string (ufile);
  struct file_descriptor *fd;
  int handle = -1;
  fd = malloc (sizeof *fd);

  if (fd != NULL)
  {
	  lock_acquire (&fs_lock);
    fd->file = filesys_open (kfile);
    if (fd->file != NULL)
    {
		  struct thread *cur = thread_current ();
      handle = fd->handle = cur->next_handle++;
      list_push_front (&cur->fds, &fd->elem);
    }
    else
    { 
      free (fd);
		}
    lock_release (&fs_lock);
  }
  
  palloc_free_page (kfile);
  return handle;
}
 
/* Returns the file descriptor associated with the given handle.
   Terminates the process if HANDLE is not associated with an
   open file. */
static struct file_descriptor *
lookup_fd (int handle)
{
  /* Add code to lookup file descriptor in the current thread's fds */
  //Added from a handout he gave in class. Hopefully this is what we are supposed to do.
  struct thread * cur = thread_current();
  struct list_elem * e;
  struct file_descriptor * fd;
  
  for(e = list_begin(&cur->fds); e != list_end(&cur->fds); e = list_next(e))
  {
    fd = list_entry(e, struct file_descriptor, elem);
    if(fd->handle == handle)
      return fd;
  }
  return NULL;
}
 
/* Filesize system call. */
static int
sys_filesize (int handle) 
{
  struct file_descriptor * fd;
  off_t rtn = -1;    

  if(handle != STDIN_FILENO)
  {
    fd = lookup_fd(handle);
		lock_acquire (&fs_lock);
    if(fd != NULL)
    {
      rtn = file_length(fd->file);
    }
  	lock_release (&fs_lock);
  }
	return rtn;
}
 
/* Read system call. */
static int
sys_read (int handle, void *udst_, unsigned size) 
{
  struct file_descriptor * fd;
	if(!verify_user(udst_))
	{
		thread_exit();
		return -1;
	}
  
  if( handle == STDIN_FILENO )
  {
		unsigned i;
    uint8_t *buffer = (uint8_t *) udst_;
    for(i = 0; i < size; i++)
		{
      buffer[i] = input_getc();
		}
		return size;
  }
  lock_acquire (&fs_lock);
  fd = lookup_fd(handle);
  if(fd == NULL)
  {
	lock_release (&fs_lock);
	return -1;
  }
  int ret = file_read(fd->file, udst_, size);
  lock_release (&fs_lock);
  return ret;
  thread_exit ();
}
 
/* Write system call. */
static int
sys_write (int handle, void *usrc_, unsigned size) 
{
  uint8_t *usrc = usrc_;
  struct file_descriptor *fd = NULL;
  int bytes_written = 0;

  /* Lookup up file descriptor. */
  if (handle != STDOUT_FILENO)
    fd = lookup_fd (handle);
  
  lock_acquire (&fs_lock);
  while (size > 0) 
    {
      /* How much bytes to write to this page? */
      size_t page_left = PGSIZE - pg_ofs (usrc);
      size_t write_amt = size < page_left ? size : page_left;
      off_t retval;

      /* Check that we can touch this user page. */
      if (!verify_user (usrc)) 
        {
          lock_release (&fs_lock);
          thread_exit ();
        }

      /* Do the write. */
      if (handle == STDOUT_FILENO)
      {
        putbuf (usrc, write_amt);
        retval = write_amt;
      }
      else
        retval = file_write (fd->file, usrc, write_amt);
      if (retval < 0) 
        {
          if (bytes_written == 0)
            bytes_written = -1;
          break;
        }
      bytes_written += retval;

      /* If it was a short write we're done. */
      if (retval != (off_t) write_amt)
        break;

      /* Advance. */
      usrc += retval;
      size -= retval;
    }
  lock_release (&fs_lock);
 
  return bytes_written;
}
 
/* Seek system call. */
static int
sys_seek (int handle, unsigned position) 
{
  struct file_descriptor * fd;
  off_t rtn = -1;    

  fd = lookup_fd(handle);
	lock_acquire (&fs_lock);
  if(fd != NULL)
  {
    file_seek(fd->file, (off_t) position);
		rtn = 1;
  }
	lock_release (&fs_lock);
	return rtn;
  //thread_exit ();
}
 
/* Tell system call. */
static int
sys_tell (int handle) 
{
  /* Add code */
  struct file_descriptor * fd;
	off_t rtn = -1;    

  fd = lookup_fd(handle);
	lock_acquire (&fs_lock);
  if(fd != NULL)
  {
    rtn = file_tell(fd->file);
  }
	lock_release (&fs_lock);

	return rtn;
}
 
/* Close system call. */
static int
sys_close (int handle) 
{
/* Add code */
  struct file_descriptor *fd;
  fd = lookup_fd (handle);
  if( fd == NULL )
	return -1;
  file_close(fd->file);
  free(fd);
  return 0;
}
 
/* On thread exit, close all open files. */
void
syscall_exit (void) 
{
/* Add code */
  struct thread * cur = thread_current();
  struct list_elem * e;
  struct file_descriptor * fd;
  
  for(e = list_begin(&cur->fds); e != list_end(&cur->fds); e = list_next(e))
  {
    fd = list_entry(e, struct file_descriptor, elem);
    lock_acquire (&fs_lock);
    file_close(fd->file);
    lock_release (&fs_lock);
	free(fd);
  }
  return;
}



struct child_process* add_child_process (int pid)
{
  struct child_process* cp = malloc(sizeof(struct child_process));
  cp->pid = pid;
  cp->load = NOT_LOADED;
  cp->wait = false;
  cp->exit = false;
  lock_init(&cp->wait_lock);
  list_push_back(&thread_current()->children,
                 &cp->elem);
  return cp;
}

struct child_process* get_child_process (int pid)
{
  struct thread *t = thread_current();
  struct list_elem *e;

  for (e = list_begin (&t->children); e != list_end (&t->children);
       e = list_next (e))
        {
          struct child_process *cp = list_entry (e, struct child_process, elem);
          if (pid == cp->pid)
         {
         return cp;
         }
        }
  return NULL;
}

void remove_child_process (struct child_process *cp)
{
  list_remove(&cp->elem);
  free(cp);
}

void remove_child_processes (void)
{
  struct thread *t = thread_current();
  struct list_elem *next, *e = list_begin(&t->children);

  while (e != list_end (&t->children))
    {
      next = list_next(e);
      struct child_process *cp = list_entry (e, struct child_process,
                                         elem);
      list_remove(&cp->elem);
      free(cp);
      e = next;
    }
}
