#define VMABASE (MAXVA>>1)

struct vma {
    struct vma *next;  // pointer to next vma in proc

    uint64 start;
    uint64 end;
    uint npages; // how many valid mmap pte in the pagetable
    
    int perm;    // pte_flags
    int prot;    // mem protection (e.g. read/write perm)
    int flags;   // whether the mapped region is visible to other process
                 // and whether write back to the underlying file
    
    struct inode *ip;
    uint off;    // offset of the file mapped to
};

// mmap management struct
struct mm {
    // vma available per process
    // default NVMA = 16
    struct vma _vma[NVMA];
    
    // Linked list. (Dummy head)
    struct vma head;       /* allocated vma list */
    struct vma freeHead;   /* free vma list */
};


