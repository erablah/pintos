#ifndef USERPROG_SYSCALL_UTIL_H
#define USERPROG_SYSCALL_UTIL_H

#include "threads/thread.h"
#include "userprog/process.h"

void halt (void);
void exit (int);
pid_t exec (const char *);
int wait (pid_t);
bool create (const char *, unsigned);
bool remove (const char *);
int open (const char *);
int filesize (int);
int read (int, void *, unsigned);
int write (int, const void *, unsigned);
void seek (int, unsigned);
unsigned tell (int);
void close (int);

void validate (void *);
void validate1 (void *);
void validate2 (void *);
void validate3 (void *);

#endif
