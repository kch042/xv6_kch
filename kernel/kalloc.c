// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define PA2PPN(pa) ((uint64)pa-KERNBASE)>>12

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// reference count to each page number
struct {
    struct spinlock lock;
    int    c[(PHYSTOP-KERNBASE) >> 12];
} refcount;

void rc_grow(uint64 pa, int i) {
    acquire(&refcount.lock);
    refcount.c[PA2PPN(pa)] += i;
    release(&refcount.lock);
}

// For uvmcowalloc()
// Do not need lock since if only one process, no race condition
// if multiple processes ref to it and read wrong rc due to race condition
// then kfree() will handle some corner cases 
int rc_onlyone(uint64 pa) {
    if (refcount.c[PA2PPN(pa)] == 1)
        return 1;
    return 0;
}

void rc_set(uint64 pa, int i) {
    acquire(&refcount.lock);
    refcount.c[PA2PPN(pa)] = i;
    release(&refcount.lock);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    // Caveat
    rc_set((uint64)p, 1);
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  
  // The decrement and checking of refcount 
  // must be atomic.
  acquire(&refcount.lock);
  if (--refcount.c[PA2PPN(pa)] > 0) {
    release(&refcount.lock);
    return;
  }
  release(&refcount.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    rc_set((uint64)r, 1);
  }
  return (void*)r;
}
