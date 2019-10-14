#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall_util.h"

static struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f)
{
  validate (f->esp);

  switch (*(int*)f->esp)
  {
    case SYS_HALT:                   /* Halt the operating system. */
    {
      halt();
      break;
    }

    case SYS_EXIT:                   /* Terminate this process. */
    {
      validate1 (f->esp);

      int status = *((int*)f->esp + 1);

      exit (status);
      break;
    }

    case SYS_EXEC:                   /* Start another process. */
    {
      validate1 (f->esp);

      char *cmd_line = (char*)*((int*)f->esp + 1);
      validate (cmd_line);

      lock_acquire (&filesys_lock);
      f->eax = exec (cmd_line);
      lock_release (&filesys_lock);
      break;
    }

    case SYS_WAIT:                   /* Wait for a child process to die. */
    {
      validate1 (f->esp);

      pid_t pid = *((pid_t*)f->esp + 1);

      f->eax = wait (pid);
      break;
    }

    case SYS_CREATE:                 /* Create a file. */
    {
      validate2 (f->esp);

      char *file = (char*)*((int*)f->esp + 1);
      unsigned initial_size = *((unsigned*)f->esp + 2);

      if (file == NULL) exit (-1);
      validate (file);

      lock_acquire (&filesys_lock);
      f->eax = create (file, initial_size);
      lock_release (&filesys_lock);
      break;
    }

    case SYS_REMOVE:                 /* Delete a file. */
    {
      validate1 (f->esp);

      char *file = (char*)*((int*)f->esp + 1);

      validate (file);

      lock_acquire (&filesys_lock);
      f->eax = remove (file);
      lock_release (&filesys_lock);
      break;
    }

    case SYS_OPEN:                   /* Open a file. */
    {
      validate1 (f->esp);

      char *file = (char*)*((int*)f->esp + 1);

      validate (file);

      lock_acquire (&filesys_lock);
      f->eax = open (file);
      lock_release (&filesys_lock);
      break;
    }

    case SYS_FILESIZE:               /* Obtain a file's size. */
    {
      validate1 (f->esp);

      int fd = *((int*)f->esp + 1);

      f->eax = filesize (fd);
      break;
    }

    case SYS_READ:                   /* Read from a file. */
    {
      validate3 (f->esp);

      int fd = *((int*)f->esp + 1);
      void *buffer = (void*)*((int*)f->esp + 2);
      unsigned size = *((unsigned*)f->esp + 3);

      validate (buffer);

      lock_acquire (&filesys_lock);
      f->eax = read (fd, buffer, size);
      lock_release (&filesys_lock);
      break;
    }

    case SYS_WRITE:                  /* Write to a file. */
    {
      validate3 (f->esp);

      int fd = *((int*)f->esp + 1);
      void *buffer = (void*)*((int*)f->esp + 2);
      unsigned size = *((unsigned*)f->esp + 3);

      validate (buffer);

      lock_acquire (&filesys_lock);
      f->eax = write (fd, buffer, size);
      lock_release (&filesys_lock);
      break;
    }

    case SYS_SEEK:                   /* Change position in a file. */
    {
      validate2 (f->esp);

      int fd = *((int*)f->esp + 1);
      unsigned position = *((unsigned*)f->esp + 2);

      seek (fd, position);
      break;
    }

    case SYS_TELL:                   /* Report current position in a file. */
    {
      validate1 (f->esp);

      int fd = *((int*)f->esp + 1);

      f->eax = tell (fd);
      break;
    }

    case SYS_CLOSE:                  /* Close a file. */
    {
      validate1 (f->esp);

      int fd = *((int*)f->esp + 1);

      close (fd);
      break;
    }

    default:
    {
      ASSERT (1);
      break;
    }
  }
}
