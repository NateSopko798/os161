#ifndef _FILE_HANDLE_H_
#define _FILE_HANDLE_H_

#include <vnode.h>
#include <uio.h>
#include <types.h>

struct file_handle {

	struct vnode *vn;
	struct lock *lk;
	off_t off;
	int counter;
	int type;
};

int file_handle_init(int fd, struct file_handle *file_table[]);
void file_handle_destroy(int fd, struct file_handle *file_table[]);

#endif /* _FILE_HANDLE_H_ */
