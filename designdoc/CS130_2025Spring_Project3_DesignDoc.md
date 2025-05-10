# CS 130: Project 3 - Virtual Memory Design Document

## Group Information

Please provide the details for your group below, including your group's name and the names and email addresses of all members.

- **Group Name**: *[printf("team")]*
- **Member 1**: Zirui Wang `<wangzr2024@shanghaitech.edu.cn>`
- **Member 2**: Xing Wu `<wuxing2024@shanghaitech.edu.cn>`

---

## Preliminaries

> If you have any preliminary comments on your submission, notes for the TAs, or extra credit, please give them here.

None.

---

## Page Table Management

### Data Structures

> **A1:** Copy here the declaration of each new or changed `struct` or struct member, global or static variable, `typedef`, or enumeration. Identify the purpose of each in 25 words or less.

#### New Struct Member to `threads/thread.h`

```c
struct thread
{
  /* ... */

  #ifdef VM
    struct list page_list; /* Pages a user process owns. */
    void *user_esp; /* Stores esp on the transition from user to kernel mode */
  #endif

  /* ... */
};
```

#### New Struct to `vm/frame.h`

```c
struct frame
{
  void *kpage;               /* Address returned by palloc. */
  void *upage;               /* Address returned to user. */
  struct thread *owner;      /* Owner thread of this frame. */
  bool pinned;               /* Whether this frame can be evicted. */
  struct hash_elem hashelem; /* The hash element in frame_hash. */
  struct list_elem listelem; /* The list element in frame_list. */
  struct page *sup_page;     /* The corresponding supplemental page. */
};
```

#### New Static Variable to `vm/frame.c`

```c
/*Frame table. */
static struct lock frame_lock;      /* A lock to protect frame table. */
static struct hash frame_hash;      /* A hash table to store frames. */
static struct list frame_list;      /* A list to evict frames.*/
static struct list_elem *clock_ptr; /* Still used to evict frames. */
```

#### New Struct and Enumeration to `vm/page.h`

```c
/* Types of a page, see comments of PAGE for more detail. */
enum page_type
{
  PAGE_UNALLOC, /* Unallocated anonymous page. */
  PAGE_ALLOC,   /* Allocated anonymous page. */
  PAGE_FILE     /* Memory mapped page. */
};

/* Page is a kind of abstraction to the memory a user program uses.

   A page can be one of difference types:
   - UNALLOC: unallocated anonymous page. it will be allocated when accessed.
   - ANON: allocated anonymous page. it is not backed by any file. it might be
     in the swap slot if evicted.
   - FILE: file backed page. it might be in the filesys if evicted or unloaded.
 */
struct page
{
  enum page_type type; /* Page type. */
  void *kpage;         /* Frame mapped to this page, if it exists. */
  slot_id slot_idx;    /* Slot store this page, if it exists. */

  /* We need some metadata to load the correct data from file. */
  struct file *file;
  off_t ofs;
  void *upage;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  bool writable;

  struct hash_elem hashelem; /* Used by sup_hash_table. */
  struct list_elem listelem; /* Used by page_list in struct thread. */
  struct thread *owner;      /* The thread that own this page. */
};
```

#### New Static Variable to `vm/page.c`

```c
/* The supplemental page table. */
static struct hash sup_page_table;

/* The lock for the supplemental page table. */
static struct lock sup_page_table_lock;
```

#### New `typedef` to `vm/swap.h`

```c
/* The swap partition is split into small slots. This is the unique identifier
   for each slot. */
typedef size_t slot_id;
```

#### New Static Variable to `vm/swap.c`

```c
/* The swap partition. */
static struct block *swap_device;

/* We use a bitmap to track allocated slots. */
static struct bitmap *swap_bitmap;

/* Use a lock to protect the swap bitmap. */
static struct lock swap_lock;
```

### Algorithms

> **A2:** In a few paragraphs, describe your code for accessing the data stored in the SPT about a given page.

*Your answer here.*

> **A3:** How does your code coordinate accessed and dirty bits between kernel and user virtual addresses that alias a single frame, or alternatively how do you avoid the issue?

*Your answer here.*

### Synchronization

> **A4:** When two user processes both need a new frame at the same time, how are races avoided?

### Rationale

> **A5:** Why did you choose the data structure(s) that you did for representing virtual-to-physical mappings?

*Your answer here.*

---

## Paging To and From Disk

### Data Structures

> **B1:** Copy here the declaration of each new or changed `struct` or struct member, global or static variable, `typedef`, or enumeration. Identify the purpose of each in 25 words or less.

None.

### Algorithms

> **B2:** When a frame is required but none is free, some frame must be evicted. Describe your code for choosing a frame to evict.

*Your answer here.*

> **B3:** When a process P obtains a frame that was previously used by a process Q, how do you adjust the page table (and any other data structures) to reflect the frame Q no longer has?

*Your answer here.*

> **B4:** Explain your heuristic for deciding whether a page fault for an invalid virtual address should cause the stack to be extended into the page that faulted.

*Your answer here.*

### Synchronization

> **B5:** Explain the basics of your VM synchronization design. In particular, explain how it prevents deadlock. (Refer to the textbook for an explanation of the necessary conditions for deadlock.)

*Your answer here.*

> **B6:** A page fault in process P can cause another process Q's frame to be evicted. How do you ensure that Q cannot access or modify the page during the eviction process? How do you avoid a race between P evicting Q's frame and Q faulting the page back in?

*Your answer here.*

> **B7:** Suppose a page fault in process P causes a page to be read from the file system or swap. How do you ensure that a second process Q cannot interfere by, for example, attempting to evict the frame while it is still being read in?

*Your answer here.*

> **B8:** Explain how you handle access to paged-out pages that occur during system calls. Do you use page faults to bring in pages (as in user programs), or do you have a mechanism for "locking" frames into physical memory, or do you use some other design? How do you gracefully handle attempted accesses to invalid virtual addresses?

*Your answer here.*

### Rationale

> **B9:** A single lock for the whole VM system would make synchronization easy but limit parallelism. On the other hand, using many locks complicates synchronization and raises the possibility for deadlock. Explain where your design falls along this continuum and why you chose to design it this way.

*Your answer here.*

---

## Memory Mapped Files

### Data Structures

> **C1:** Copy here the declaration of each new or changed `struct` or struct member, global or static variable, `typedef`, or enumeration. Identify the purpose of each in 25 words or less.

#### New Struct Member to `threads/thread.h`

```c
struct thread
{
  /* ... */

  #ifdef VM
    mapid_t mapid_next; /* Next mapid for this process. */
  #endif

  /* ... */
};
```

#### New Struct to `userprog/sycall.h`

```c
struct mmap_data
{
  mapid_t mapping;           /* Unique identifier within a process */
  struct file *file;         /* File mapped to this memory segment. */
  struct hash_elem hashelem; /* The hash element in mmap_table. */
  tid_t owner;               /* Owner process of this mapping. */
  void *uaddr;               /* Begin of mapped memory address. */
};
```

### Algorithms

> **C2:** Describe how memory mapped files integrate into your virtual memory subsystem. Explain how the page fault and eviction processes differ between swap pages and other pages.

*Your answer here.*

> **C3:** Explain how you determine whether a new file mapping overlaps any existing segment.

*Your answer here.*

### Rationale

> **C4:** Mappings created with `mmap` have similar semantics to those of data demand-paged from executables, except that `mmap` mappings are written back to their original files, not to swap. This implies that much of their implementation can be shared. Explain why your implementation either does or does not share much of the code for the two situations.

*Your answer here.*

---

## Survey Questions

> Do you have any suggestions for the TAs to more effectively assist students, either for future quarters or the remaining projects? Any other comments?

*Your answer here.*
