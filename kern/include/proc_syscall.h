#ifndef _PROC_SYSCALL_H_
#define _PROC_SYSCALL_H_

#include <types.h>
#include <kern/limits.h>

extern struct proc *proc_table[250];
extern struct lock *proc_table_lock;
extern int exit_table[128];
struct trapframe;
extern struct lock *buf_lock;

pid_t sys_fork(int *retval, struct trapframe *tf);
void sys_exit(int exitcode);
pid_t sys_getpid(int *retval);
pid_t sys_waitpid(pid_t pid, int *status, int options, int *retval);
int sys_execv(const char *program, char **args, int *retval);
int sys_sbrk(intptr_t amount, int *retval);
pid_t next_available_pid(void);
#endif /* _PROC_SYSCALL_H_ */
