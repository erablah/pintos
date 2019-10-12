#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

void halt (void)
{
  shutdown_power_off ();
}

void exit (int status)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  struct list list = cur->lock_list;
  for (e = list_begin (&list); e != list_end (&list); e = list_remove (e))
    {
      struct lock *lock = list_entry (e, struct lock, elem);
      lock_release (lock);
    }
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

pid_t exec (const char *cmd_line)
{
  return process_execute (cmd_line);
}

int wait (pid_t pid)
{
  return process_wait (pid);
}

bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);

int write (int fd, const void *buffer, unsigned size)
{

}

void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

void validate (void *ptr)
{
  struct thread *cur = thread_current ();

  if (!is_user_vaddr (ptr) || ptr == NULL || pagedir_get_page (cur->pagedir , ptr) == NULL)
    exit (-1);
}
