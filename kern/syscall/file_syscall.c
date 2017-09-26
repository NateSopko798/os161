#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <vnode.h>
#include <uio.h>
#include <current.h>
#include <proc.h>
#include <kern/errno.h>
#include <synch.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <kern/unistd.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <endian.h>

int
sys_open(const char *filename, int flags, int *retval)
{
    size_t actual = 0;
    int result;
    
    if(filename == NULL){
        *retval = -1;
        return EFAULT;
    }
    
    if(flags > 63){
        *retval = -1;
        return EINVAL;
    }
    
    char file_name_copy[150];
    if(copyinstr((const_userptr_t)filename, file_name_copy, 150, &actual)){
        kfree(file_name_copy);
        *retval = -1;
        return EFAULT;
    }
    
    for (int i=0; i <= OPEN_MAX; i++ ) {
        if(curproc->file_table[i] == NULL){
//            file_handle_init(i, curproc->file_table);
            curproc->file_table[i] = kmalloc(sizeof(struct file_handle));
            if (curproc->file_table[i] == NULL) {
                kfree(file_name_copy);
                *retval = ENOMEM;
                return -1;
            }
            curproc->file_table[i]->lk = lock_create("lock");
            if (curproc->file_table[i]->lk == NULL) {
                curproc->file_table[i] = NULL;
                kfree(file_name_copy);
                kfree(curproc->file_table[i]);
                *retval = ENOMEM;
                return -1;
            }
            curproc->file_table[i]->counter = 1;
            curproc->file_table[i]->off = 0;
            result = vfs_open(file_name_copy, flags, 0, &curproc->file_table[i]->vn);
            if(result){
                file_handle_destroy(i, curproc->file_table);
                kfree(file_name_copy);
                *retval = -1;
                return result;
            }
            
            if(flags == 5){
                curproc->file_table[i]->type = 1;
            }
            if(flags == 4){
                curproc->file_table[i]->type = 2;
            }

            *retval = i;
            return 0;
        }
    }
    kfree(file_name_copy);
    *retval = -1;
    return ENFILE;
}

ssize_t
sys_read(int fd, void *buf, size_t buflen, int *retval)
{
    struct uio u;
    struct iovec iov;
    int result;
    
    if(fd < 0 || fd >= OPEN_MAX || curproc->file_table[fd] == NULL){
        *retval = -1;
        return EBADF;
    }
    
    if(curproc->file_table[fd]->type == 1){
        *retval = -1;
        return EBADF;
    }
    
    lock_acquire(curproc->file_table[fd]->lk);
    
    void *buf_copy = kmalloc(sizeof(buf));
    if(buf_copy == NULL){
        lock_release(curproc->file_table[fd]->lk);
        *retval = -1;
        return EFAULT;
    }
    
    result = copyin(buf, buf_copy, sizeof(buf));
    if(result){
        lock_release(curproc->file_table[fd]->lk);
        kfree(buf_copy);
        *retval = -1;
        return EFAULT;
    }

    uio_uinit(&iov, &u, buf, buflen, curproc->file_table[fd]->off, UIO_READ, curproc->p_addrspace);

    result = VOP_READ(curproc->file_table[fd]->vn, &u);
    if (result) {
        lock_release(curproc->file_table[fd]->lk);
        kfree(buf_copy);
        *retval = -1;
        return result;
    }

    curproc->file_table[fd]->off = u.uio_offset;
	lock_release(curproc->file_table[fd]->lk);
    kfree(buf_copy);
    *retval = buflen - u.uio_resid;
	return 0;
}


ssize_t
sys_write(int fd, void *buf, size_t buflen, int *retval)
{
	struct uio u;
	struct iovec iov;
	int result;

	if(fd < 0 || fd >= OPEN_MAX || curproc->file_table[fd] == NULL){
        *retval = -1;
        return EBADF;
    }
    
    if(curproc->file_table[fd]->type == 2){
        *retval = -1;
        return EBADF;
    }

	lock_acquire(curproc->file_table[fd]->lk);
    
    void *buf_copy = kmalloc(sizeof(buf));
    if(buf_copy == NULL){
        lock_release(curproc->file_table[fd]->lk);
        *retval = -1;
        return EFAULT;
    }
    
    result = copyin(buf, buf_copy, sizeof(buf));
    if(result){
        lock_release(curproc->file_table[fd]->lk);
        kfree(buf_copy);
        *retval = -1;
        return EFAULT;
    }
    
	uio_uinit(&iov, &u, buf, buflen, curproc->file_table[fd]->off, UIO_WRITE, curproc->p_addrspace);

	result = VOP_WRITE(curproc->file_table[fd]->vn, &u);
    if (result) {
		*retval = -1;
        kfree(buf_copy);
		lock_release(curproc->file_table[fd]->lk);
		return result;
    }

	curproc->file_table[fd]->off = u.uio_offset;
    lock_release(curproc->file_table[fd]->lk);
    kfree(buf_copy);
	*retval = buflen - u.uio_resid;
	return 0;
}

int
sys_close(int fd, int *retval)
{
	if(fd < 0 || fd >= OPEN_MAX || curproc->file_table[fd] == NULL){
                *retval = -1;
                return EBADF;
    }
//    kprintf("sys close is called on %d",fd);
//    (void)fd;
//    (void)retval;
//	lock_acquire(curproc->file_table[fd]->lk);
//    curproc->file_table[fd]->counter--;
//	if(curproc->file_table[fd]->counter == 0){
//		vfs_close(curproc->file_table[fd]->vn);
//		lock_release(curproc->file_table[fd]->lk);
//		lock_destroy(curproc->file_table[fd]->lk);
//		kfree(curproc->file_table[fd]);
//		curproc->file_table[fd] = NULL;
//		return 0;
//	}
//    	lock_release(curproc->file_table[fd]->lk);
        return 0;
}

off_t
sys_lseek(int fd, off_t pos, int *retval1, int *whence, int *retval)
{
    int result;

	if(fd < 0 || fd >= OPEN_MAX || curproc->file_table[fd] == NULL){
		*retval = -1;
		return EBADF;
	}

    if(!VOP_ISSEEKABLE(curproc->file_table[fd]->vn)){
    	*retval = -1;
        return ESPIPE;
    }

    if(whence == NULL){
        *retval = -1;
        return EINVAL;
    }

    lock_acquire(curproc->file_table[fd]->lk);

	off_t new_seek_val;
	struct stat *bufstat;
	new_seek_val = curproc->file_table[fd]->off;
	bufstat = kmalloc(sizeof(*bufstat));
    if(bufstat == NULL){
        lock_release(curproc->file_table[fd]->lk);
        *retval = -1;
        return ENOMEM;
    }

	result = VOP_STAT(curproc->file_table[fd]->vn, bufstat);
    	if(result){
            lock_release(curproc->file_table[fd]->lk);
            kfree(bufstat);
        	*retval = -1;
        	return result;
    	}

	switch(*whence) {
        case SEEK_SET: new_seek_val = pos;
                    break;
        case SEEK_CUR: new_seek_val += pos;
                    break;
        case SEEK_END: new_seek_val = bufstat->st_size + pos;
                    break;
        default:    lock_release(curproc->file_table[fd]->lk);
                    kfree(bufstat);
                    *retval = -1;
                    return EINVAL;

	}

	if(new_seek_val < 0){
		lock_release(curproc->file_table[fd]->lk);
        kfree(bufstat);
        *retval = -1;
        return EINVAL;
    }
	curproc->file_table[fd]->off = new_seek_val;
	//split64to32(curproc->file_table[fd]->off, *retval, *retval1);
    lock_release(curproc->file_table[fd]->lk);
    kfree(bufstat);
	*retval = (uint32_t)(curproc->file_table[fd]->off >> 32);
	*retval1 = (uint32_t)(curproc->file_table[fd]->off & 0xFFFFFFFFLL);
	return 0;
}

int
sys_chdir(const char *pathname, int *retval)
{
    char *path;
    size_t actual = 0;
    int result;
    
    path = (char *)kmalloc(sizeof(char)*NAME_MAX);
    if(path == NULL){
        *retval = -1;
        return EFAULT;
    }
    
    if(copyinstr((const_userptr_t)pathname, path, NAME_MAX, &actual)){
        kfree(path);
        *retval = -1;
        return EFAULT;
    }
    
    path = kstrdup(pathname);
    if (path == NULL) {
        kfree(path);
        *retval = -1;
        return EFAULT;
    }
    
    result = vfs_chdir(path);
    if(result){
        kfree(path);
        *retval = -1;
        return result;
    }
    return 0;
}

int
sys_getcwd(char *buf, size_t buflen, int *retval)
{
    
    struct uio u;
    struct iovec iov;
    int result;
    
    uio_uinit(&iov, &u, buf, buflen, 0, UIO_READ, curproc->p_addrspace);
    
    result = vfs_getcwd(&u);
    if (result) {
        *retval = -1;
        return result;
    }
    
    *retval = buflen - u.uio_resid;
    return 0;
}

int
sys_dup2(int oldfd, int newfd, int *retval)
{
    int temp = 0;

    if(oldfd < 0 || oldfd >= OPEN_MAX || curproc->file_table[oldfd] == NULL){
        *retval = -1;
        return EBADF;
    }

    if(newfd < 0 || newfd >= OPEN_MAX){
        *retval = -1;
        return EBADF;
    }
    
    if(newfd == -1){
        *retval = -1;
        return ENFILE;
    }
    
    if (newfd == oldfd) {
        return 0;
    }

    lock_acquire(curproc->file_table[oldfd]->lk);
    if(curproc->file_table[newfd] != NULL){
        int result = sys_close(newfd, &temp);
        if(result){
            *retval = -1;
            return result;
        }
    }
    curproc->file_table[newfd] = curproc->file_table[oldfd];
    curproc->file_table[oldfd]->counter++;
    *retval = newfd;
    lock_release(curproc->file_table[oldfd]->lk);
	return 0;
}
