#include "userprog/syscall_util.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include <console.h>
#include "devices/input.h"
#include "userprog/process.h"
#include <stdio.h>
#include <string.h>

struct file * fd_to_file (int fd);

void halt (void)
{
  shutdown_power_off ();
}

void exit (int status)
{
  struct thread *cur = thread_current ();
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
  struct file *file_ptr = filesys_open (file);

  if (file_ptr == NULL)
    return -1;

  return file_ptr->fd;
}

// what if invalid fd? assertion in file_length () will be called
int filesize (int fd)
{
  struct file *file_ptr = fd_to_file (fd);

  if (file_ptr == NULL)
    exit (-1);

  return file_length (file_ptr);
}

int read (int fd, void *buffer, unsigned size)
{
  if (fd == 0)
  {
    while (size > 0)
    {
      *(uint8_t*)buffer = input_getc ();
      buffer++;
      size--;
    }
    return size;
  }

  if (fd == 1)
    exit (-1);

  struct file *file_ptr = fd_to_file (fd);

  if (file_ptr == NULL)
    exit (-1);

  return file_read (file_ptr, buffer, size);
}

int write (int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
  {
    putbuf (buffer, size);
    return (int)size;
  }

  if (fd == 0)
    exit (-1);

  struct file *file_ptr = fd_to_file (fd);

  if (file_ptr == NULL)
    exit (-1);

  return file_write (file_ptr, buffer, size);

}

void seek (int fd, unsigned position)
{
  struct file *file_ptr = fd_to_file (fd);

  if (file_ptr == NULL)
    exit (-1);

  file_seek (file_ptr, position);
}

unsigned tell (int fd)
{
  struct file *file_ptr = fd_to_file (fd);

  if (file_ptr == NULL)
    exit (-1);

  return file_tell (file_ptr);
}

void close (int fd)
{
  struct file *file_ptr = fd_to_file (fd);

  file_close (file_ptr); //handles NULL
}

void
validate (void *ptr)
{
  for (int i = 0; i < 4; i++)
  {
    if (!is_user_vaddr (ptr + i) || (ptr + i) == NULL)
      {
        exit (-1);
      }
  }
}

void
validate1 (void *ptr)
{
  for (int i = 4; i < 8; i++)
  {
    if (!is_user_vaddr (ptr + i) || (ptr + i) == NULL)
      exit (-1);
  }
}

void
validate2 (void *ptr)
{
  validate1 (ptr);
  for (int i = 8; i < 12; i++)
  {
    if (!is_user_vaddr (ptr + i) || (ptr + i) == NULL)
      exit (-1);
  }
}

void
validate3 (void *ptr)
{
  validate1 (ptr);
  validate2 (ptr);
  for (int i = 12; i < 16; i++)
  {
    if (!is_user_vaddr (ptr + i) || ptr + i == NULL)
      exit (-1);
  }
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
