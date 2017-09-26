#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <signal.h>
#include <synch.h>
#include <proc_syscall.h>
#include <kern/fcntl.h>
#include <kern/stat.h>
#include <vfs.h>
#include <vnode.h>
#include <bitmap.h>

#define KERNELPAGE 1
#define USERPAGE 2

struct coremap_entry *coremap;
int total_coremap_entries;
struct lock *proc_table_lock;
int bytes_used;
struct bitmap *disk;
bool swapping_enabled;
struct vnode *disk_vnode;
int page_to_evict;
int start_point;
struct lock *bitmap_lock;

//static struct spinlock coremap_spinlock = SPINLOCK_INITIALIZER;

void
vm_bootstrap(void)
{
    int result;
    int err;
    struct stat disk_stat;
//    char *disk_name;
    spinlock_init(&coremap_spinlock);
    proc_table_lock = lock_create("proc_table_lock");
    bitmap_lock = lock_create("bit_map_lock");
    
//    disk_stat = kmalloc(sizeof(struct stat));
//    if(disk_stat == NULL){
//        swapping_enabled = false;
//        return;
//    }
    
//    disk_name = kstrdup("lhd0raw:");
    char disk_name[] = "lhd0raw:";
    result = vfs_open(disk_name, O_RDWR, 0, &disk_vnode);
    if(result){
        swapping_enabled = false;
        return;
    }
    err = VOP_STAT(disk_vnode, &disk_stat);
    if(err){
        swapping_enabled = false;
        return;
    }
    
    disk = bitmap_create(disk_stat.st_size/PAGE_SIZE);
    
    swapping_enabled = true;
    
}

//static
paddr_t
getppages(unsigned long npages, int status, struct page_table_entry *pte)
{
    int result;
    uint32_t ehi, elo;
    spinlock_acquire(&coremap_spinlock);
    bool found_pages = false;
    for(int i = 0; i<total_coremap_entries; i++){
        if(!coremap[i].page_status){
            found_pages = true;
            for(int j = i; j < (int)(i + npages); j++){
                if(coremap[j].page_status != 0){
                    i = j;
                    found_pages = false;
                    break;
                }
                if(j == (int)npages){
                    break;
                }
                if(j >= total_coremap_entries){
                    spinlock_release(&coremap_spinlock);
                    return 0;
                }
            }
            if(found_pages){
                for(int temp = i; temp < (int)(i +npages); temp++){
                    coremap[temp].page_status = status;
                }
                coremap[i].chunk_size = npages;
                coremap[i].pte = pte;
                bytes_used += (npages * PAGE_SIZE);
                paddr_t retval1 = i * PAGE_SIZE;
                bzero((void *)PADDR_TO_KVADDR(retval1), npages * PAGE_SIZE);
                spinlock_release(&coremap_spinlock);
                return (i * PAGE_SIZE);
            }
        }
    }
    if(swapping_enabled){
        page_to_evict++;
        while(coremap[page_to_evict].page_status != USERPAGE || (coremap[page_to_evict].recently_used == true && coremap[page_to_evict].page_status == USERPAGE)){
            //while(coremap[page_to_evict].page_status != USERPAGE ){
            coremap[page_to_evict].recently_used = false;
            if(page_to_evict > total_coremap_entries-1){
                page_to_evict = start_point;
            }
            page_to_evict++;
        }
        KASSERT(coremap[page_to_evict].page_status == USERPAGE);
        KASSERT(coremap[page_to_evict].pte != NULL);
        int retval = page_to_evict;
        
        coremap[retval].recently_used = true;
        
        struct page_table_entry *old_pte = coremap[retval].pte;
        coremap[retval].pte = pte;
        coremap[retval].page_status = status;
        spinlock_release(&coremap_spinlock);
        
        lock_acquire(bitmap_lock);
        unsigned int place_on_disk = 0;
        result = bitmap_alloc(disk,&place_on_disk);
        if (result) {
            lock_release(bitmap_lock);
            return result;
        }
        lock_release(bitmap_lock);
        
        lock_acquire(old_pte->lk);
        struct page_table_entry *current_page_table;
        current_page_table = old_pte;
        current_page_table->offset = place_on_disk*PAGE_SIZE;
        current_page_table->state = true;
        KASSERT(bitmap_isset(disk, place_on_disk) != 0);
        block_write(retval*PAGE_SIZE, place_on_disk*PAGE_SIZE);
        
        ehi = old_pte->vpn;
        elo = old_pte->ppn | TLBLO_DIRTY | TLBLO_VALID;
        int i = tlb_probe(ehi, elo);
        if(i < 0){
            
        } else {
            tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
        }
        
        lock_release(old_pte->lk);
        bzero((void *)PADDR_TO_KVADDR(retval*PAGE_SIZE), PAGE_SIZE);
        return (retval*PAGE_SIZE);
    } else {
        spinlock_release(&coremap_spinlock);
        return 0;
    }
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
    paddr_t pa;
    //    spinlock_acquire(&coremap_spinlock);
    pa = getppages(npages,1, NULL);
    //    spinlock_release(&coremap_spinlock);
    if (pa==0) {
        return 0;
    }
    return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
    spinlock_acquire(&coremap_spinlock);
    int start_point = KVADDR_TO_PADDR(addr) / PAGE_SIZE;
    int chunk = coremap[start_point].chunk_size;
    bytes_used -= (chunk * PAGE_SIZE);
    for(int i = start_point; i < start_point + chunk; i++){
        coremap[i].chunk_size = 0;
        coremap[i].page_status = 0;
    }
    spinlock_release(&coremap_spinlock);
    
}

unsigned
int
coremap_used_bytes() {
    
    return bytes_used;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
    (void)ts;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    bool vaddr_in_segment = false;
    bool vaddr_in_page_table = false;
    struct addrspace *as;
    paddr_t paddr = 0;
    uint32_t ehi, elo;
    int spl;
    //    struct uio u;
    //    struct iovec iov;
    
    faultaddress &= PAGE_FRAME;
    
    switch (faulttype) {
        case VM_FAULT_READONLY:
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
            break;
        default:
            return EINVAL;
    }
    
    if (curproc == NULL) {
        return EFAULT;
    }
    
    as = proc_getas();
    
    if (as == NULL) {
        return EFAULT;
    }
    
    
    struct region *current_region = as->rbase;
    while(current_region != NULL){
        if(faultaddress >= current_region->region_start_address && faultaddress < current_region->region_start_address + current_region->region_size){
            vaddr_in_segment = true;
            break;
        } else {
            current_region = current_region->next;
        }
    }
    
    if(vaddr_in_segment){
        struct page_table_entry *current_page_table = as->ptbase;
        while(current_page_table != NULL){
            if(faultaddress == current_page_table->vpn){
                lock_acquire(current_page_table->lk);
                vaddr_in_page_table = true;
                paddr = current_page_table->ppn;
                if(swapping_enabled){
                    
                    if(current_page_table->state == true){
                        
                        KASSERT(current_page_table->lk != NULL);
                        paddr_t new_page = getppages(1,3, current_page_table);
                        block_read(new_page, current_page_table->offset);
                        off_t temp_off = current_page_table->offset/PAGE_SIZE;
                        current_page_table->state = false;
                        paddr = new_page;
                        current_page_table->ppn = new_page;
                        spl = splhigh();
                        ehi = faultaddress;
                        elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
                        int result = tlb_probe(ehi, elo);
                        if(result < 0){
                            tlb_random(ehi, elo);
                        } else {
                            tlb_write(ehi, elo, result);
                        }
                        splx(spl);
                        coremap[paddr/PAGE_SIZE].recently_used = true;
                        coremap[paddr/PAGE_SIZE].page_status = 2;
                        lock_release(current_page_table->lk);
                        
                        lock_acquire(bitmap_lock);
                        bitmap_unmark(disk, temp_off);
                        lock_release(bitmap_lock);
                        
//                        spinlock_acquire(&coremap_spinlock);
//                        coremap[paddr/PAGE_SIZE].recently_used = true;
//                        spinlock_release(&coremap_spinlock);
                        
                        return 0;
                    }
                }
                lock_release(current_page_table->lk);
                break;
            } else {
                current_page_table = current_page_table->next;
            }
        }
    } else {
        return EFAULT;
    }
    
    if(vaddr_in_page_table){
//        spinlock_acquire(&coremap_spinlock);
//        coremap[paddr/PAGE_SIZE].recently_used = true;
//        spinlock_release(&coremap_spinlock);
        spl = splhigh();
        ehi = faultaddress;
        elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
        int result = tlb_probe(ehi, elo);
        if(result < 0){
            tlb_random(ehi, elo);
        } else {
            tlb_write(ehi, elo, result);
        }
        coremap[paddr/PAGE_SIZE].recently_used = true;
        splx(spl);
        return 0;
        
    } else {
        struct page_table_entry *new_pte = kmalloc(sizeof(struct page_table_entry));
        
        if(new_pte == NULL){
            return ENOMEM;
        }
        new_pte->vpn = faultaddress;
        new_pte->lk = lock_create("swapping lock");
        if(new_pte->lk == NULL){
            return ENOMEM;
        }
        KASSERT(new_pte->lk != NULL);
        lock_acquire(new_pte->lk);
        new_pte->ppn = getppages(1,3, new_pte);
        if(new_pte->ppn == 0){
            return ENOMEM;
        }
        if(new_pte->ppn == 0){
            kfree(new_pte);
            return ENOMEM;
        }
        new_pte->offset = -1;
        new_pte->state = false;
        new_pte->next = as->ptbase;
        as->ptbase = new_pte;
        spl = splhigh();
        ehi = faultaddress;
        elo = new_pte->ppn | TLBLO_DIRTY | TLBLO_VALID;
        int result = tlb_probe(ehi, elo);
        if(result < 0){
            tlb_random(ehi, elo);
        } else {
            tlb_write(ehi, elo, result);
        }
        coremap[new_pte->ppn/PAGE_SIZE].recently_used = true;
        coremap[new_pte->ppn/PAGE_SIZE].page_status = 2;
        splx(spl);
        lock_release(new_pte->lk);
        return 0;
    }
    
    return 0;
}

int
block_write(paddr_t place_on_memory, off_t place_on_disk)
{
    struct uio u;
    struct iovec iov;
    uio_kinit(&iov, &u, (void *)PADDR_TO_KVADDR(place_on_memory), PAGE_SIZE, place_on_disk, UIO_WRITE);
    int result = VOP_WRITE(disk_vnode, &u);
    if (result) {
        return result;
    }
    return 0;
}

int
block_read(paddr_t place_on_memory, off_t place_on_disk)
{
    struct uio u;
    struct iovec iov;
    uio_kinit(&iov, &u, (void *)PADDR_TO_KVADDR(place_on_memory), PAGE_SIZE, place_on_disk, UIO_READ);
    int result = VOP_READ(disk_vnode, &u);
    if(result){
        return result;
    }
    return 0;
}
