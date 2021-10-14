#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.


typedef long Align;

union header {
  struct {
    union header *ptr;
    uint size;
  } s;
  
  // Align is never used
  // Just to force each header to be at least 8 byte
  // since sizeof(Align) = 8 bytes
  Align x;
};

typedef union header Header;

static Header base;     /* empty list to get started */
static Header *freep;   /* start of free list */

void
free(void *ap)
{
  Header *bp, *p;

  bp = (Header*)ap - 1;
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;

  if(bp + bp->s.size == p->s.ptr){
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;
  
  if(p + p->s.size == bp){
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  
  freep = p;
}

static Header*
morecore(uint nu)
{
  char *p;
  Header *hp;

  if(nu < 4096)
    nu = 4096;
  
  p = sbrk(nu * sizeof(Header));
  if(p == (char*)-1)
    return 0;
  
  hp = (Header*)p;
  hp->s.size = nu;

  free((void*)(hp + 1));
  
  return freep;
}

/* general-purpose memory allocator */
void*
malloc(uint nbytes)
{
  Header *p, 
         *prevp;
  uint    nunits;
  int     is_allocating;
  void   *ret;

  // Required number of units of headers
  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  
  prevp = freep;
  if(prevp == 0){        /* No free list yet */
    base.s.ptr  = &base;
    freep       = &base;
    prevp       = &base;
    base.s.size = 0;
  }


  is_allocating = 1;
  p = prevp->s.ptr;

  while(is_allocating) {
    // Big enough
    if(p->s.size >= nunits){
      
      // Exactly
      if(p->s.size == nunits)
        prevp->s.ptr = p->s.ptr;
      
      else {
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }

      freep = prevp;
      is_allocating = 0;
      ret = p + 1;
    }
    
    // Wrapped around free list
    if(p == freep) {
        p = morecore(nunits);
        if (p == 0) {
            ret = 0;
            is_allocating = 0;
        }
    }

    prevp = p;
    p = p->s.ptr;
  }

  return ret;
}
