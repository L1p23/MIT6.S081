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

#define NBUCKETS 13

struct {
  struct spinlock bucket_lock[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKETS];
  struct spinlock lock;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  char name[10];
  for (int i = 0; i < NBUCKETS; i++) {
    snprintf(name, 10, "bcache_%d", i);
    initlock(&bcache.bucket_lock[i], name);
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  // Create linked list of buffers
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    int bucket = b->blockno % NBUCKETS;
    b->next = bcache.head[bucket].next;
    b->prev = &bcache.head[bucket];
    initsleeplock(&b->lock, "buffer");
    bcache.head[bucket].next->prev = b;
    bcache.head[bucket].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int bucket = blockno % NBUCKETS;
  acquire(&bcache.bucket_lock[bucket]);

  // Is the block already cached?
  for(b = bcache.head[bucket].next; b != &bcache.head[bucket]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket_lock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head[bucket].prev; b != &bcache.head[bucket]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.bucket_lock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucket_lock[bucket]);
  // 保证只有一个线程在steal，避免死锁
  acquire(&bcache.lock);

  // 多个线程访问同一个blockno的情况下，
  // 一个线程能够进入steal，其他线程阻塞在102行。
  // 在第一个线程为该blockno分配buf并释放lock后，
  // 其余线程会轮流进入steal部分尝试再次为其分配，
  // 而此时该块已经分配完成，
  // 需要先检查是否分配完成来避免多次分配同一个blockno
  // 检查是否已经从别的bucket中分配block
  acquire(&bcache.bucket_lock[bucket]);
  // Is the block already cached?
  for(b = bcache.head[bucket].next; b != &bcache.head[bucket]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket_lock[bucket]);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucket_lock[bucket]);

  // steal from other buckets
  for (int i = 0; i < NBUCKETS; i++) {
    if (i == bucket)
      continue;
    acquire(&bcache.bucket_lock[i]);
    for (b = bcache.head[i].prev; b != &bcache.head[i]; b = b->prev) {
      if (b->refcnt == 0) {
        // move buffer
        b->next->prev = b->prev;
        b->prev->next = b->next;
        release(&bcache.bucket_lock[i]);
        
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        acquire(&bcache.bucket_lock[bucket]);
        b->next = bcache.head[bucket].next;
        b->prev = &bcache.head[bucket];
        bcache.head[bucket].next->prev = b;
        bcache.head[bucket].next = b;

        release(&bcache.bucket_lock[bucket]);
        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.bucket_lock[i]);
  }
  
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
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  int bucket = b->blockno % NBUCKETS;

  acquire(&bcache.bucket_lock[bucket]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[bucket].next;
    b->prev = &bcache.head[bucket];
    bcache.head[bucket].next->prev = b;
    bcache.head[bucket].next = b;
  }
  
  release(&bcache.bucket_lock[bucket]);
}

void
bpin(struct buf *b) {
  int bucket = b->blockno % NBUCKETS;
  acquire(&bcache.bucket_lock[bucket]);
  b->refcnt++;
  release(&bcache.bucket_lock[bucket]);
}

void
bunpin(struct buf *b) {
  int bucket = b->blockno % NBUCKETS;
  acquire(&bcache.bucket_lock[bucket]);
  b->refcnt--;
  release(&bcache.bucket_lock[bucket]);
}