#include <proc_syscall.h>
#include <types.h>
#include <lib.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <syscall.h>
#include <limits.h>
#include <synch.h>
#include <addrspace.h>
#include <kern/wait.h>
#include <mips/trapframe.h>
#include <kern/fcntl.h>
#include <vm.h>
#include <vfs.h>
#include <test.h>
#include <copyinout.h>
#include <spl.h>
#include <mips/tlb.h>

static char buf[ARG_MAX];

pid_t
sys_fork(int *retval, struct trapframe *tf)
{
//    lock_acquire(curproc->lk);
    int result;
    
    struct proc *child_proc = proc_child_create("child_proc");
    if(child_proc == NULL){
//        lock_release(curproc->lk);
        *retval = -1;
        return ENOMEM;
    }
    
    struct trapframe *child_tf = kmalloc(sizeof(*child_tf));
    if(child_tf == NULL){
//        lock_release(curproc->lk);
        *retval = -1;
        return ENOMEM;
    }
    
    result = as_copy(curproc->p_addrspace, &child_proc->p_addrspace);
    if(result){
//        lock_release(curproc->lk);
        kfree(child_tf);
        *retval = -1;
        return result;
    }
    
    if(child_proc->pid == -1){
//        lock_release(curproc->lk);
        kfree(child_tf);
        *retval = -1;
        return ENPROC;
    }
    
    memcpy(child_tf, tf, sizeof(struct trapframe));
    
//    lock_release(curproc->lk);
    
    result = thread_fork("child_thread" , child_proc, enter_forked_process, child_tf, (unsigned long)NULL);
    if(result){
        kfree(child_tf);
        *retval = -1;
        return result;
    }
    
    if (curproc->p_cwd != NULL) {
        VOP_INCREF(curproc->p_cwd);
        child_proc->p_cwd = curproc->p_cwd;
        curproc->p_numthreads++;
    }
    
    *retval = child_proc->pid;
    return 0;
}

pid_t
sys_getpid(int *retval)
{
    *retval = curproc->pid;
    return 0;
}

pid_t
sys_waitpid(pid_t pid, int *status, int options, int *retval)
{
//    kprintf("this was called %d\n", pid);
    if(status == NULL){
        *retval = pid;
        return 0;
    }
    
    if(options != 0){
        *retval = -1;
        return EINVAL;
    }
    if(pid < 0 || pid > 250 || proc_table[pid] == NULL){
        *retval = -1;
        return ESRCH;
    }
    
    if(proc_table[pid]->ppid != curproc->pid){
        *retval = -1;
        return ECHILD;
    }
    
    P(proc_table[pid]->sem);
    
    int result = copyout(&proc_table[pid]->exit_code, (userptr_t)status, sizeof(int));
    if(result){
        *retval = -1;
        return result;
    }
    
    *retval = pid;
    proc_destroy(proc_table[pid]);
//    for (int i = 0; i<OPEN_MAX; i++){
//        if(proc_table[pid]->file_table[i] !=NULL){
//            lock_acquire(proc_table[pid]->file_table[i]->lk);
//            proc_table[pid]->file_table[i]->counter -= 1;
////            kprintf("%d\n",proc_table[pid]->file_table[i]->counter);
//            if(proc_table[pid]->file_table[i]->counter == 0){
//                vfs_close(proc_table[pid]->file_table[i]->vn);
//                lock_release(proc_table[pid]->file_table[i]->lk);
//                lock_destroy(proc_table[pid]->file_table[i]->lk);
//                kfree(proc_table[pid]->file_table[i]);
//                proc_table[pid]->file_table[i] = NULL;
//            }
//            else{
//                lock_release(proc_table[pid]->file_table[i]->lk);
//            }
//        }
//    }
//    sem_destroy(proc_table[pid]->sem);
//    lock_destroy(proc_table[pid]->lk);
//    as_destroy(proc_table[pid]->p_addrspace);
//    kfree(proc_table[pid]->p_name);
//    proc_table[pid]->p_addrspace = NULL;
//    kfree(proc_table[pid]);
//    proc_table[pid] = NULL;
    return 0;
}

void
sys_exit(int exitcode)
{
//    if(curproc->pid == 1){
//        int temp = 0;
//        for(int i=0; i < OPEN_MAX; i++){
//            sys_close(i, &temp);
//        }
//    }
    curproc->exit_status  = true;
    curproc->exit_code = _MKWAIT_EXIT(exitcode);
    V(curproc->sem);
    thread_exit();
    
}

int
sys_execv(const char *program, char **args, int *retval)
{
    struct addrspace *new_as = NULL;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result= 0;
    char *program_copy;
    size_t actual = 0;
    int argc = 0;
    int padding_size = 0;
    
    lock_acquire(buf_lock);
    
    // copy arguments from user space to kernel space
    
    memset(buf,'\0',ARG_MAX);
    
    //    program_copy = (char *)kmalloc(sizeof(char)*PATH_MAX);
    
    program_copy = kstrdup(program);
    if (program_copy == NULL) {
        kfree(program_copy);
        lock_release(buf_lock);
        *retval = -1;
        return EFAULT;
    }
    
    if(copyinstr((const_userptr_t)program, program_copy, PATH_MAX, &actual)){
        lock_release(buf_lock);
        kfree(program_copy);
        *retval = -1;
        return EFAULT;
    }
    
    void *off = buf;
    char **start_args = args;
    
    void *temp = kmalloc(sizeof(char *));
    if(temp == NULL){
        lock_release(buf_lock);
        kfree(program_copy);
        *retval = -1;
        return ENOMEM;
    }
    
    result = copyin((userptr_t)start_args, temp, sizeof(char *));
    if(result){
        lock_release(buf_lock);
        kfree(program_copy);
        kfree(temp);
        *retval = -1;
        return EFAULT;
    }
    
    while(start_args[argc] != NULL){
        result = copyin((userptr_t)start_args, temp, sizeof(char *));
        if(result){
            lock_release(buf_lock);
            kfree(program_copy);
            kfree(temp);
            *retval = -1;
            return EFAULT;
        }
        argc++;
    }
    argc++;
    
    void *m = off;
    
    for(int i = 0; i < argc - 1 ; i++){
        result = copyinstr((const_userptr_t)args[i], off, ARG_MAX, &actual);
        if(result){
            lock_release(buf_lock);
            kfree(program_copy);
            kfree(temp);
            *retval = -1;
            return result;
        }
        off += actual;
        if(actual % 4){
            padding_size = 4 - (actual % 4);
            off += padding_size;
        }
    }
    
    
    // load executable into memory
    result = vfs_open(program_copy, O_RDONLY, 0, &v);
    if (result) {
        lock_release(buf_lock);
        kfree(program_copy);
        kfree(temp);
        *retval = -1;
        return result;
    }
    
    if(curproc->p_addrspace != NULL){
        as_destroy(curproc->p_addrspace);
        curproc->p_addrspace = NULL;
    }
    
    KASSERT(curproc->p_addrspace == NULL);
    
    new_as = as_create();
    if (new_as == NULL) {
        vfs_close(v);
        kfree(program_copy);
        kfree(temp);
        lock_release(buf_lock);
        *retval = -1;
        return ENOMEM;
    }
    
    curproc->p_addrspace = new_as;
    as_activate();
    
    result = load_elf(v, &entrypoint);
    if (result) {
        vfs_close(v);
        kfree(program_copy);
        kfree(temp);
        lock_release(buf_lock);
        *retval = -1;
        return result;
    }
    
    vfs_close(v);
    
    result = as_define_stack(new_as, &stackptr);
    if (result) {
        kfree(program_copy);
        kfree(temp);
        lock_release(buf_lock);
        *retval = -1;
        return result;
    }
    
    // copy arguments from kernel to userspace
    
    char **argv = kmalloc(argc * sizeof(char *));
    void *start_argv = argv;
    //        void *temp = NULL;
    argv[argc-1] = NULL;
    
    int counter = 0;
    int n = 0;
    int point = 0;
    for(int i = 0; i < ARG_MAX; i++){
        counter++;
        if(buf[i] == '\0'){
            if(counter % 4){
                n = 4 - (counter % 4);
                counter += n;
            }
            stackptr -= counter;
            argv[point] = (char *)stackptr;
            copyout(m, (userptr_t)stackptr, counter);
            m += counter;
            i += n;
            point++;
            counter = 0;
            n = 0;
            if(buf[i+1] == '\0'){
                break;
            }
        }
    }
    
    stackptr -= argc * 4;
    argv = start_argv;
    
    result = copyout(argv, (userptr_t)stackptr, argc * 4);
    if(result){
        kfree(program_copy);
        kfree(temp);
        kfree(argv);
        lock_release(buf_lock);
        *retval = -1;
        return result;
    }
    kfree(program_copy);
    kfree(temp);
    kfree(argv);
    lock_release(buf_lock);
    
    enter_new_process(argc-1, (userptr_t)stackptr, NULL, stackptr, entrypoint);
    
    *retval = 0;
    return 0;
}

pid_t
next_available_pid(void)
{
    lock_acquire(proc_table_lock);
    for(int i= PID_MIN; i<128; i++){
        if(proc_table[i] == NULL){
            lock_release(proc_table_lock);
            return i;
        }
    }
    lock_release(proc_table_lock);
    return -1;
}

int
sys_sbrk(intptr_t amount, int * retval){
    
    if(amount % PAGE_SIZE !=0){
        *retval = -1;
        return EINVAL;
    }
    struct addrspace * as = proc_getas();
    vaddr_t old = as->heap_end;
    if(amount == 0){
        *retval = old;
        return 0;
    }
    else if(amount > 0){
        if(as->heap_end + amount >= (USERSTACK - (1024 * PAGE_SIZE))){
            *retval = -1;
            return ENOMEM;
        }
        struct region * current = as->rbase;
        struct region * next;
        while(current != NULL){
            next = current->next;
            if(current->region_start_address == as->heap_start){
                current->region_size += amount;
            }
            current = next;
        }
        as->heap_end += amount;
        *retval = old;
        return 0;
    }
    else{
        if(as->heap_end + amount < as->heap_start || amount == -4096*1024*256){
            *retval = -1;
            return EINVAL;
        }
        int size = (amount / PAGE_SIZE);
        //-------------------------------------------------------------------------------------------------------------------------------
        struct page_table_entry * current = as->ptbase;
        struct page_table_entry * next;
        struct page_table_entry * prev = NULL;
        while(current != NULL){
            next = current->next;
            if(current->vpn > (as->heap_end + amount) && current->vpn < old){
                free_kpages(PADDR_TO_KVADDR(current->ppn));
                lock_destroy(current->lk);
                kfree(current);
                if(prev != NULL){
                    prev->next = next;
                }
                else{
                    as->ptbase = next;
                }
            }
            else{
                prev = current;
            }
            current = next;
        }
        //-------------------------------------------------------------------------------------------------------------------------------
        struct region * current1 = as->rbase;
        struct region * next1;
        while(current1 != NULL){
            next1 = current1->next;
            if(current1->region_start_address == as->heap_start){
                current1->region_size += amount;
            }
            current1 = next1;
        }
        as->heap_end += (size * PAGE_SIZE);
        as_activate();
        *retval = old;
        return 0;
    }
    return EINVAL;
}
