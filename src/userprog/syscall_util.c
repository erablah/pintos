#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include <console.c>
#include "devices/input.h"
#include "userprog/process.h"

void halt (void)
{
  shutdown_power_off ();
}

void exit (int status)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  struct list lock_list = cur->lock_list;
  struct list file_list = cur->file_list;

  for (e = list_begin (&lock_list); e != list_end (&lock_list);
        e = list_remove (e))
  {
    struct lock *lock = list_entry (e, struct lock, elem);
    lock_release (lock);
  }

  /* Close the executable file (allow it to be written on) and
  free all fd's */
  file_close (cur->execfile);
  for (e = list_begin (&file_list); e != list_end (&file_list);
        e = list_remove (e))
  {
    struct file *file = list_entry (e, struct file, elem);
    file_close (file);
  }

  cur->exit_status = status;
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

bool create (const char *file, unsigned initial_size)
{
  return filesys_create (file, initial_size);
}

bool remove (const char *file)
{
  return filesys_remove (file);
}

int open (const char *file)
{
  struct thread *cur = thread_current ();
  struct file *file_ptr = filesys_open (file);

  if (file_ptr == NULL)
    return -1;

  return file_ptr->fd;
}

// what if invalid fd? assertion in file_length () will be called
int filesize (int fd)
{
  struct file *file_ptr = fd_to_file (fd);
  return file_length (file_ptr);
}

int read (int fd, void *buffer, unsigned size)
{
  if (fd == 0)
  {
    input_getc ();
  }

  struct file *file_ptr = fd_to_file (fd);
  return file_read (file_ptr, buffer, size);
}

int write (int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
  {
    putbuf (buffer, size);
  }

  struct file *file_ptr = fd_to_file (fd);
  return file_write (file_ptr, buffer, size);

}

void seek (int fd, unsigned position)
{
  struct file *file_ptr = fd_to_file (fd);
  file_seek (file_ptr, position);
}

unsigned tell (int fd)
{
  struct file *file_ptr = fd_to_file (fd);
  return file_tell (file_ptr);
}

void close (int fd)
{
  struct file *file_ptr = fd_to_file (fd);
  file_close (file_ptr);
}

void
validate (void *ptr)
{
  struct thread *cur = thread_current ();

  if (!is_user_vaddr (ptr) || ptr == NULL || pagedir_get_page (cur->pagedir , ptr) == NULL)
    exit (1);
}

struct file *
fd_to_file (int fd)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&cur->file_list); e != list_end (&cur->file_list);
       e = list_next (e))
  {
    struct file *f = list_entry (e, struct file, elem);
    if (f->fd == fd)
      return f;
  }
  return NULL;
}
