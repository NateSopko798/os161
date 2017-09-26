/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <mips/tlb.h>
#include <synch.h>
#include <bitmap.h>

struct lock *bitmap_lock;
//static char buffer[PAGE_SIZE];


/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
    struct addrspace *as;
    
    as = kmalloc(sizeof(struct addrspace));
    if (as == NULL) {
        return NULL;
    }
    
    as->heap_start = 0;
    as->heap_end = 0;
    as->rbase = NULL;
    as->ptbase = NULL;
    
    return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
    
    struct addrspace *newas;
    
    newas = as_create();
    if (newas==NULL) {
        return ENOMEM;
    }
    
    int i = 0;
    int result;
    
    newas->heap_start = old->heap_start;
    newas->heap_end = old->heap_end;
    
    struct page_table_entry *current = old->ptbase;
    struct page_table_entry *last;
    last = NULL;
    while(current != NULL){
        struct page_table_entry *new;
        new = kmalloc(sizeof(struct page_table_entry));
        if(new == NULL){
            as_destroy(newas);
            return ENOMEM;
        }
        new->lk = lock_create("swapping lock");
        if(new->lk == NULL){
            return ENOMEM;
        }
        
        new->vpn = current->vpn;
        if(swapping_enabled == true){
            lock_acquire(current->lk);
            lock_acquire(new->lk);
            if(current->state == true){
                paddr_t temp_page = getppages(1,3,new);
                if(temp_page == 0){
                    return ENOMEM;
                }
                block_read(temp_page, current->offset);
                unsigned int place_on_disk = 0;
                lock_acquire(bitmap_lock);
                result = bitmap_alloc(disk,&place_on_disk);
                if (result) {
                    lock_release(bitmap_lock);
                    lock_release(new->lk);
                    return result;
                }
                lock_release(bitmap_lock);
                block_write(temp_page, place_on_disk*PAGE_SIZE);
                new->state = true;
                new->offset = place_on_disk*PAGE_SIZE;
                new->next = NULL;
                if(i == 0){
                    newas->ptbase = new;
                }
                else{
                    last->next = new;
                }
                last = new;
                free_kpages(PADDR_TO_KVADDR(temp_page));
                lock_release(current->lk);
                current = current->next;
                i++;
//                spinlock_acquire(&coremap_spinlock);
//                coremap[temp_page/PAGE_SIZE].page_status = 0;
//                spinlock_release(&coremap_spinlock);
            } else {
                spinlock_acquire(&coremap_spinlock);
                coremap[current->ppn/PAGE_SIZE].page_status = 3;
                spinlock_release(&coremap_spinlock);
                unsigned int place_on_disk = 0;
                lock_acquire(bitmap_lock);
                result = bitmap_alloc(disk,&place_on_disk);
                if (result) {
                    return result;
                }
                lock_release(bitmap_lock);
                block_write(current->ppn, place_on_disk*PAGE_SIZE);
                new->state = true;
                new->offset = place_on_disk*PAGE_SIZE;
                new->next = NULL;
                if(i == 0){
                    newas->ptbase = new;
                }
                else{
                    last->next = new;
                }
                last = new;
                lock_release(current->lk);
                spinlock_acquire(&coremap_spinlock);
                coremap[current->ppn/PAGE_SIZE].page_status = 2;
                spinlock_release(&coremap_spinlock);
                current = current->next;
                i++;
            }
            lock_release(new->lk);
            
        }
        else{
            new->vpn = current->vpn;
            lock_acquire(new->lk);
            new->ppn = getppages(1,3, new);
            if(new->ppn == 0){
                as_destroy(newas);
                return ENOMEM;
            }
            memmove((void *)PADDR_TO_KVADDR(new->ppn),
                    (const void *)PADDR_TO_KVADDR(current->ppn),
                    PAGE_SIZE);
            new->state = current->state;
            new->offset = current->offset;
            lock_release(new->lk);
            new->next = NULL;
            if(i == 0){
                newas->ptbase = new;
            }
            else{
                last->next = new;
            }
            last = new;
            current = current->next;
            i++;
        }
    }
    
    i = 0;
    struct region * current1 = old->rbase;
    struct region * last1;
    last1 = NULL;
    while(current1 != NULL){
        struct region * new1;
        new1 = kmalloc(sizeof(struct region));
        if(new1 == NULL){
            as_destroy(newas);
            return ENOMEM;
        }
        new1->region_start_address = current1->region_start_address;
        new1->region_size = current1->region_size;
        new1->region_permission = current1->region_permission;
        new1->next = current1->next;
        if(i==0){
            newas->rbase = new1;
        }
        else{
            last1->next = new1;
        }
        last1 = new1;
        current1 = current1->next;
        i++;
    }
    
    *ret = newas;
    return 0;
}

void
as_destroy(struct addrspace *as)
{
    //    (void)as;
    //    return;
    struct page_table_entry * current_page_table = as->ptbase;
    struct page_table_entry *next_page_table;
    
    
    if(swapping_enabled == true){
        while(current_page_table != NULL){
            lock_acquire(current_page_table->lk);
            if(current_page_table->state == true){
                bitmap_unmark(disk, current_page_table->offset/PAGE_SIZE);
            } else {
//                coremap[current_page_table->ppn/PAGE_SIZE].page_status = 3;
                free_kpages(PADDR_TO_KVADDR(current_page_table->ppn));
            }
            lock_release(current_page_table->lk);
            lock_destroy(current_page_table->lk);
            next_page_table = current_page_table->next;
            kfree(current_page_table);
            current_page_table = next_page_table;
        }
    } else {
        while(current_page_table != NULL){
            next_page_table = current_page_table->next;
            free_kpages(PADDR_TO_KVADDR(current_page_table->ppn));
            lock_destroy(current_page_table->lk);
            kfree(current_page_table);
            current_page_table = next_page_table;
        }
    }
    
    struct region * current_region = as->rbase;
    struct region * next_region;
    
    while(current_region != NULL){
        next_region = current_region->next;
        kfree(current_region);
        current_region = next_region;
    }
    //    }
    kfree(as);
    
}

void
as_activate(void)
{
    
    int i, spl;
    struct addrspace *as;
    
    as = proc_getas();
    if (as == NULL) {
        return;
    }
    
    /* Disable interrupts on this CPU while frobbing the TLB. */
    spl = splhigh();
    
    for (i=0; i<NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
    
    splx(spl);
}

void
as_deactivate(void)
{
    /*
     * Write this. For many designs it won't need to actually do
     * anything. See proc.c for an explanation of why it (might)
     * be needed.
     */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
                 int readable, int writeable, int executable)
{
    
    size_t npages;
    
    /* Align the region. First, the base... */
    memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
    vaddr &= PAGE_FRAME;
    
    /* ...and now the length. */
    memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;
    
    npages = memsize / PAGE_SIZE;
    
    struct region *new_head;
    
    new_head = kmalloc(sizeof(struct region));
    if (new_head == NULL) {
        return ENOMEM;
    }
    
    //    kprintf("This is the start address: %x \n",vaddr);
    
    struct region *head;
    head = as->rbase;
    new_head->region_start_address = vaddr;
    //    kprintf("This is the end address : %x \n",vaddr + memsize);
    new_head->region_size = npages * PAGE_SIZE;;
    new_head->next = head;
    as->rbase = new_head;
    
    as->heap_start = as->rbase->region_start_address + memsize;
    as->heap_end = as->heap_start;
    
    (void)readable;
    (void)writeable;
    (void)executable;
    return 0;
}

int
as_prepare_load(struct addrspace *as)
{
    /*
     * Write this.
     */
    
    (void)as;
    return 0;
}

int
as_complete_load(struct addrspace *as)
{
    /*
     * Write this.
     */
    
    struct region *new_head = kmalloc(sizeof(struct region));
    if(new_head == NULL){
        as_destroy(as);
        return ENOMEM;
    }
    struct region *head;
    head = as->rbase;
    new_head->region_start_address = as->heap_start;
    new_head->region_size = 0;
    new_head->next = head;
    as->rbase = new_head; //this is for initializing the heap whenever we want to.
    
    (void)as;
    return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
    /*
     * Write this.
     */
    (void)as;
    
    struct region *new_head;
    
    new_head = kmalloc(sizeof(struct region));
    if (new_head == NULL) {
        return ENOMEM;
    }
    
    struct region *head;
    head = as->rbase;
    new_head->region_start_address = USERSTACK - 1024 * PAGE_SIZE;
    new_head->region_size = 1024 * PAGE_SIZE;
    new_head->next = head;
    as->rbase = new_head;
    //
    //	/* Initial user-level stack pointer */
    *stackptr = USERSTACK;
    
    return 0;
}
