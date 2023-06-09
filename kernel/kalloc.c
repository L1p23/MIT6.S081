// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

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

#define PA2IDX(pa) ((pa-KERNBASE)/PGSIZE)
#define PA_NUMS PA2IDX(PHYSTOP)
int refcount[PA_NUMS];

struct spinlock reflock;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&reflock, "reflock");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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

  acquire(&reflock);
  if (--refcount[PA2IDX((uint64)pa)] > 0) {
    release(&reflock);
    return;
  }
  //refcount[PA2IDX((uint64)pa)] = 0;
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
  release(&reflock);
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
    if (holding(&reflock))
      refcount[PA2IDX((uint64)r)] = 1;
    else {
      acquire(&reflock);
      refcount[PA2IDX((uint64)r)] = 1;
      release(&reflock);
    }
  }
  return (void*)r;
}

void
refpa(uint64 pa)
{
  acquire(&reflock);
  ++refcount[PA2IDX(pa)];
  release(&reflock);
}

int
getref(uint64 pa)
{
  acquire(&reflock);
  int ref = refcount[PA2IDX(pa)];
  release(&reflock);
  return ref;
}

uint64
copypa(uint64 pa)
{
  acquire(&reflock);
  if (refcount[PA2IDX(pa)] <= 1) {
    release(&reflock);
    return pa;
  }
  char *ka = kalloc();
  if (ka == 0) {
    release(&reflock);
    return 0;
  }
  memmove(ka, (char *)pa, PGSIZE);
  --refcount[PA2IDX(pa)];
  release(&reflock);
  return (uint64)ka;
}

int
emptypage()
{
  struct run *r;
  acquire(&kmem.lock);
  r = kmem.freelist;
  int count = 0;
  while (r) {
    count++;
    r = r->next;
  }
  release(&kmem.lock);
  return count;
}