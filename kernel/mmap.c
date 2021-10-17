#include "param.h"
#include "types.h"
#include "fcntl.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "mmap.h"
#include "proc.h"
#include "defs.h"

// Idea:
//   For each mapped file, maintain the physical pages for MAP_SHARED mappings, 
//   which are allocated based on the file offset.
//
//   mmap() only sets up vma and allocates va to each process.
//
//   When user accesses the allocated va, page fault occurs.
//   The handler first finds the vma and then the file it mapped to.
//   Then it checks if the faulted page is in mem by looking up the inode struct.
//   Map the pa to the faulted va in pagetable.
//   If the file page is in mem, just use the found pa to map
//   Else, allocate a page and read the file page from disk, then map.


// Debugging.
// Log the free list and allocated list.
void printHead() {
    struct proc *p = myproc();
    struct vma *v;
    int free = 0, head = 0;
    
    v = &p->mm.freeHead;
    while (v) {
        free++;
        v = v->next;
    }

    v = &p->mm.head;
    while (v) {
        head++;
        v = v->next;
    }
    
    printf("#head: %d\n#free: %d\n", head, free);
}

void
initmm(struct proc *p) {
    struct vma *v;
    
    p->mm.head.start = VMABASE;
    p->mm.head.end = VMABASE;
    p->mm.head.next = 0;

    // Create linked list of freevma
    for (v = p->mm._vma; v < p->mm._vma + NVMA; v++) {
        v->next = p->mm.freeHead.next;
        p->mm.freeHead.next = v;
    }
}

// Simple implementation mmap
// 
// Check the protection bits and perm
// Allocate page-aligned virtual addr (but not phys addr until write occurs),
// which starts from VMABASE = (MAXVA >> 1)
uint64
mmap(uint64 addr, uint len, int prot, int flags, struct file *f, uint off) {
    int pte_flags = PTE_U;
    
    // The file must be writable to enable MAP_SHARED flags
    // since modification to the content would be written back to the file  
    if (prot & PROT_WRITE) {
        if ((flags & MAP_SHARED) && !f->writable)
            return -1;
        pte_flags |= PTE_W;
    }
    if (prot & PROT_READ) {
        if (!f->readable)
            return -1;
        pte_flags |= PTE_R;
    }
   
    // Find an free vma
    struct proc *p = myproc();
    struct vma *v = p->mm.freeHead.next;
    if (v == 0) {
        printHead();
        panic("mmap: out of vma");
    }
        
    p->mm.freeHead.next = v->next;
    v->next = 0;

    struct vma *pv = &p->mm.head;    /* current last vma in vma list */
    while (pv->next)
        pv = pv->next;
    
    // Allocate virtual address for new vma
    // TODO: improve vma va management (collect and compress va from unmmaped vma)
    v->start = PGROUNDUP(pv->end);
    if (v->start + len >= PGROUNDDOWN(MAXVA - 2*PGSIZE))
        panic("mmap: out of va for new vma");
    v->end = v->start + len;
    
    // Insert to the end of vma list
    pv->next = v;  

    v->flags = flags;
    v->prot = prot;
    v->perm = pte_flags;
    v->npages = 0;

    v->ip = f->ip;
    v->off = off;
    mdup(v->ip, v->flags & MAP_SHARED);  
    
    //printHead();

    return v->start;
}


// get the physical page for faulted mmap va
// If shared mapping, look up the inode to see if the page is cached
uint64 getmpa(struct vma *v, uint64 va) {
    // compute the file offset the va mapped to
    uint off = (v->off + va - v->start);
    uint64 pa = 0;
    struct inode *ip = v->ip;
    
    ilock(ip);
    
    // the va should not exceeds file size
    if (PGROUNDDOWN(v->off + va - v->start) > PGROUNDDOWN(ip->size))
        return 0;

    if (v->flags & MAP_SHARED)
        pa = ip->mpa[off/PGSIZE];
    if (pa != 0) {
        iunlock(ip);
        return pa;    /* shared page cached */
    }
    
    // not cached or MAP_PRIVATE
    // Allocate a new page.
    pa = (uint64) kalloc();
    if (pa == 0)  panic("getmpa(): kalloc() out of pa");
    memset((char*)pa, 0, PGSIZE);

    readi(ip, 0, pa, off, PGSIZE);

    if (v->flags & MAP_SHARED)
        ip->mpa[off/PGSIZE] = pa;
    
    iunlock(ip);
    return pa;
}

// Handleds mmap page fault.
// (mmap is copy-on-write)
// Allocate and add pte to the pagetable
// when process needs it.
int
mmaphandler(uint64 va) {
    //printf("handler begins: faulted va: %p\n", va);
 
    // the va should be page-aligned to work well
    va = PGROUNDDOWN(va);

    uint64 pa;    
    struct proc *p = myproc();
    struct vma *v = p->mm.head.next;
    
    // Find the vma
    while (v) {
        if (va >= v->start && va < v->end)
            break;
        v = v->next;
    }
    if (v == 0) return -1;

    pa = getmpa(v, va);
    if (pa == 0)
        return -1;
    
    // map the va to the allocated page
    if (mappages(p->pagetable, va, PGSIZE, pa, v->perm) < 0)
        panic("mmaphandler: mappages");
    v->npages++;
    
    return 0;
}

// Unmap and free the mmaped pages. 
// va should be page-aligned.
void
_unmap(struct vma *v, uint64 va, uint len) {
    struct proc *p = myproc();
    uint64 cur_va = va;
    uint64 pa;
    
    // unmap one page one time
    while (v->npages > 0 && cur_va < va + len) {
        pa = walkaddr(p->pagetable, cur_va);
        
        // skip if no pa allocated for this va
        if (pa > 0)  {
            uvmunmap(p->pagetable, cur_va, 1, 1);
            v->npages--; 
        }

        cur_va += PGSIZE;
    }
}


// Unmap and write back the mmaped pages to the underlying file.
// The mmap pages are not freed.
//
// va should be page-aligned
// The vma should be MAP_SHARED
void
_unmap_writeback(struct vma *v, uint64 va, uint len) {
    struct proc *p = myproc();
    uint64 cur_va = va;
    uint64 pa;

    begin_op();
    ilock(v->ip);
    
    // write back and unmap one page one time
    while (v->npages > 0 && cur_va < va + len) {
        pa = walkaddr(p->pagetable, cur_va);
        
        // unmap and write back
        if (pa > 0) {
            if (writei(v->ip, 0, pa, v->off + cur_va - v->start, PGSIZE) < PGSIZE)
                panic("munmap: write back error");
            
            uvmunmap(p->pagetable, cur_va, 1, 0);
            v->npages--;
        } 

        cur_va += PGSIZE;
    }
   
    iunlock(v->ip);
    end_op();   
}


// Free all cached file pages (i.e. mmaped pages).
// Caller must hold ip->lock
void
mfree(struct inode *ip) {
    uint off = 0;
    uint64 pa = 0;

    while (off < ip->size) {
        pa = ip->mpa[off/PGSIZE];
        if (pa != 0) 
            kfree((void*)pa);
        
        ip->mpa[off/PGSIZE] = 0;
        off += PGSIZE;   
    }
    
    //memset(ip->mpa, 0, sizeof(ip->mpa));  // clear the mpa table
}

uint64
munmap(uint64 va, uint len) {
    struct proc *p = myproc();
    struct vma *v = p->mm.head.next;
    struct inode *ip;

    // unmapped va must be page aligned
    if (va % PGSIZE != 0)
        panic("munmap: va not aligned");

    // Find the vma that the va belongs to
    while (v) {
        if (va >= v->start && va < v->end)
            break;
        v = v->next;
    }
    if (v == 0)     // va not mapped 
        return 0;    
    
    // Update the vma start, end and off
    if (va == v->start) {
        v->start += len;
        v->off += len;
    }
    if (va + len >= v->end) {
        len = v->end - va;   // do not unmap out of range 
        v->end = va;
    }
    
    // Unmap the specified pages.
    // Write back to the backing file if MAP_SHARED
    // Free the pages if MAP_PRIVATE
    if (v->flags & MAP_SHARED)
        _unmap_writeback(v, va, len);
    else
        _unmap(v, va, len);
    
    // mmap area all unmapped
    // Free all mapped pages if shared and no more process has shared mapping to it
    // close the file and free the vma
    if (v->start >= v->end) {
        // make sure all mmap pte are cleared and MAP_PRIVATE pages are freed
        if (v->npages > 0) {
            printHead();
            printf("v->npages: %d\n", v->npages);
            panic("munmap: still has page not unmapped");
        }
        
        // Free the shared mmap pages
        if (v->flags & MAP_SHARED) {
            ip = v->ip;
            ilock(ip);
            if (--ip->nshare == 0)
                mfree(ip);
            //printf("ip->nshare: %d\n", ip->nshare);
            iunlock(ip);
        }
        
        // decrease the ref count
        begin_op();
        iput(v->ip);
        end_op();

        // free the vma
        struct vma *pv = &p->mm.head;
        while (pv->next != v)
            pv = pv->next;
        pv->next = v->next;
        v->next = p->mm.freeHead.next;
        p->mm.freeHead.next = v;
    }

    return 0;
}

/*
void
mwriteback(struct vma *v) {
    // Only shared mappings needs to be written back
    if (!(v->flags & MAP_SHARED))
        return;
    
    struct inode *ip = v->file->ip;
    uint off;
    uint64 pa;

    // Decrement the shared mapping count to the inode
    ilock(ip);
    if (--ip->nshare > 0) {
        iunlock(ip);
        return;
    }

    // No more process has mappings to this inode, 
    // free all cached file page in the memory
    begin_op();
    for (off = 0; off < ip->size; off += PGSIZE) {
        pa = ip->mpa[off/PGSIZE];
        if (pa > 0) {
            writei(ip, 0, pa, off, PGSIZE);
            kfree((void*)pa);
        }
        ip->mpa[off/PGSIZE] = 0;
    }
    end_op();
}
*/


