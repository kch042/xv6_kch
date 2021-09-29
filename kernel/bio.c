// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

// Original design: a single linked list of bufs
// Better design: Hash table with a link list of bufs as one hash bucket

struct {
  struct spinlock lock[NBUCKET];
  struct buf buf[NBUF];

  // Linked lists of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKET];
} bcache;

// hash function for the buckets
int bhash(uint blockno) {
    return blockno % NBUCKET;
}

void
binit(void)
{
  struct buf *b;

  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.lock[i], "bcache");
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  // Create linked lists of buffers
  // Add to the buckets as evenly as possible
  uint j = 0;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head[j].next;
    b->prev = &bcache.head[j];
    initsleeplock(&b->lock, "buffer");
    bcache.head[j].next->prev = b;
    bcache.head[j].next = b;

    // prevent bstrip from panic
    b->blockno =j;

    j = bhash(j+1);
  }
}

// Insert to the head of bucket.
// Lock should be held before enter
void
binsert(struct buf *b, int buckno) {
  if (!holding(&bcache.lock[buckno]))
      panic("binsert: lock not held before enter");
  b->next = bcache.head[buckno].next;
  b->prev = &bcache.head[buckno];
  bcache.head[buckno].next->prev = b;
  bcache.head[buckno].next = b;
}

// Strip a buf from the bucket list.
// Lock of the bucket from which buf is stripped
// should be held before enter.
void
bstrip(struct buf *b) {
  int buckno = bhash(b->blockno);
  if (!holding(&bcache.lock[buckno]))
      panic("bstrip: lock not held before enter");
  if (b == &bcache.head[buckno])
      panic("bstrip: only head, nothing to strip");

  b->next->prev = b->prev;
  b->prev->next = b->next;
}

// strip an free buf from the given bucket
// and return it
static struct buf*
bgetfree(int buckno) {
  struct buf *b;
  
  acquire(&bcache.lock[buckno]);

  b = bcache.head[buckno].prev;
  // TODO: since LRU, maybe can stop if b != 0 ?
  while (b != &bcache.head[buckno]) {
    if (b->refcnt == 0) {
      b->refcnt = 1;
      b->valid = 0;
      
      // strip from the bucket
      bstrip(b);

      release(&bcache.lock[buckno]);
      return b;
    }
    b = b->prev;
  }
  
  // no free buf
  release(&bcache.lock[buckno]);
  return 0;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int buckno = bhash(blockno);
  
  acquire(&bcache.lock[buckno]);

  // Is the block already cached?
  for(b = bcache.head[buckno].next; b != &bcache.head[buckno]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;

      // insert b to head of the list for LRU
      bstrip(b);
      binsert(b, buckno);
      
      release(&bcache.lock[buckno]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.lock[buckno]);


  // Not cached.
  // Look for free buf from every bucket
  int i = NBUCKET;
  int curbuck = buckno;
  while (i--) {

    b = bgetfree(curbuck);

    if (b) {
      acquire(&bcache.lock[buckno]);

      b->dev = dev;
      b->blockno = blockno;  // now b should be hashed to our bucket
      
      binsert(b, buckno);
     
      release(&bcache.lock[buckno]);
      acquiresleep(&b->lock);
      return b;
    }
    
    // No free buf in this bucket, go to next bucket
    curbuck = (curbuck+1) % NBUCKET;
  }

  /*
  for(b = bcache.head[buckno].prev; b != &bcache.head[buckno]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[buckno]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  */

  // TODO: no enough free buf, replace a buf using LRU principle
  // need to acquire sleeplock of that buf first

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  
  int buckno = bhash(b->blockno);
  acquire(&bcache.lock[buckno]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    /*  original design: move to head
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
    */

    bstrip(b);
    
    // insert at the end
    b->next = &bcache.head[buckno];
    b->prev = bcache.head[buckno].prev;
    bcache.head[buckno].prev->next = b;
    bcache.head[buckno].prev = b;
  }
  
  release(&bcache.lock[buckno]);
}

void
bpin(struct buf *b) {
  int buckno = bhash(b->blockno);
  acquire(&bcache.lock[buckno]);
  b->refcnt++;
  release(&bcache.lock[buckno]);
}

void
bunpin(struct buf *b) {
  int buckno = bhash(b->blockno);
  acquire(&bcache.lock[buckno]);
  b->refcnt--;
  release(&bcache.lock[buckno]);
}


