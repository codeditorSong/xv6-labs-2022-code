// Buffer cache.
//
// The buffer cache is a hash table of buf structures holding
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

struct {
  struct spinlock lock;
  struct buf head;
} bcache[NBUCKET];

struct buf bufs[NBUF];

// Serializes eviction (finding an unused buffer across buckets)
struct spinlock eviction_lock;

void
binit(void)
{
  struct buf *b;

  initlock(&eviction_lock, "bcache.eviction");

  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache[i].lock, "bcache.bucket");
    bcache[i].head.next = &bcache[i].head;
    bcache[i].head.prev = &bcache[i].head;
  }

  // Put all buffers into bucket 0 initially
  for(b = bufs; b < bufs + NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->next = bcache[0].head.next;
    b->prev = &bcache[0].head;
    bcache[0].head.next->prev = b;
    bcache[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket = blockno % NBUCKET;

  acquire(&bcache[bucket].lock);

  // Is the block already cached in its bucket?
  for(b = bcache[bucket].head.next; b != &bcache[bucket].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache[bucket].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached. Try to find an unused buffer in the same bucket.
  for(b = bcache[bucket].head.next; b != &bcache[bucket].head; b = b->next){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache[bucket].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache[bucket].lock);

  // Eviction path: search other buckets for an unused buffer.
  // Serialize eviction to avoid races.
  acquire(&eviction_lock);

  // Recheck target bucket (another thread might have cached the block
  // or freed a buffer in this bucket while we were waiting).
  acquire(&bcache[bucket].lock);
  for(b = bcache[bucket].head.next; b != &bcache[bucket].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache[bucket].lock);
      release(&eviction_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  for(b = bcache[bucket].head.next; b != &bcache[bucket].head; b = b->next){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache[bucket].lock);
      release(&eviction_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache[bucket].lock);

  // Search all other buckets for an unused buffer.
  // Always acquire locks in order of bucket index to avoid deadlock.
  struct buf *found = 0;
  int found_bucket = -1;
  for(int i = 0; i < NBUCKET; i++){
    if(i == bucket) continue;
    acquire(&bcache[i].lock);
    for(b = bcache[i].head.next; b != &bcache[i].head; b = b->next){
      if(b->refcnt == 0) {
        // Remove from source bucket
        b->next->prev = b->prev;
        b->prev->next = b->next;
        found = b;
        found_bucket = i;
        break;
      }
    }
    if(found){
      release(&bcache[i].lock);
      break;
    }
    release(&bcache[i].lock);
  }

  if(found == 0)
    panic("bget: no buffers");

  // Add to target bucket (eviction_lock is held, so no concurrent eviction)
  acquire(&bcache[bucket].lock);
  // Recheck: block might have been cached by a cache-hit in another thread
  for(b = bcache[bucket].head.next; b != &bcache[bucket].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache[bucket].lock);
      release(&eviction_lock);
      // Return the found buffer back to its original bucket
      acquire(&bcache[found_bucket].lock);
      found->next = bcache[found_bucket].head.next;
      found->prev = &bcache[found_bucket].head;
      bcache[found_bucket].head.next->prev = found;
      bcache[found_bucket].head.next = found;
      release(&bcache[found_bucket].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  found->dev = dev;
  found->blockno = blockno;
  found->valid = 0;
  found->refcnt = 1;
  found->next = bcache[bucket].head.next;
  found->prev = &bcache[bucket].head;
  bcache[bucket].head.next->prev = found;
  bcache[bucket].head.next = found;
  release(&bcache[bucket].lock);
  release(&eviction_lock);
  acquiresleep(&found->lock);
  return found;
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
// No LRU list manipulation needed.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bucket = b->blockno % NBUCKET;
  acquire(&bcache[bucket].lock);
  b->refcnt--;
  release(&bcache[bucket].lock);
}

void
bpin(struct buf *b) {
  int bucket = b->blockno % NBUCKET;
  acquire(&bcache[bucket].lock);
  b->refcnt++;
  release(&bcache[bucket].lock);
}

void
bunpin(struct buf *b) {
  int bucket = b->blockno % NBUCKET;
  acquire(&bcache[bucket].lock);
  b->refcnt--;
  release(&bcache[bucket].lock);
}