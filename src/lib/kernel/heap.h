#ifndef __LIB_KERNEL_HEAP_H
#define __LIB_KERNEL_HEAP_H

/* Pairing Heap.

  A data structure that satisfies the heap property by using merge operation.

 Operations supported:
   - Push: Push a new element to the heap.
   - Pop: Remove the root element and return it.
   - Top: Return the root element without removing it. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Heap element. */
struct heap_elem
{
  struct heap_elem *child;   /* Child element. */
  struct heap_elem *sibling; /* Next brother element. */
};

/* Converts pointer to heap element HEAP_ELEM into a pointer to
   the structure that HEAP_ELEM is embedded inside.  Supply the
   name of the outer structure STRUCT and the member name MEMBER
   of the heap element. */
#define heap_entry(HEAP_ELEM, STRUCT, MEMBER)                                 \
  ((STRUCT *)((uint8_t *)&(HEAP_ELEM)->child                                  \
              - offsetof (STRUCT, MEMBER.child)))

/* Compares the value of two heap elements A and B, given
   auxiliary data AUX.  Returns true if A is less than B, or
   false if A is greater than or equal to B. */
typedef bool heap_less_func (const struct heap_elem *lhs,
                             const struct heap_elem *rhs, void *aux);

/* Heap. */
struct heap
{
  size_t size;           /* current number of elements in the heap. */
  struct heap_elem *top; /* Heap top, which is the greatest element. */
  heap_less_func *less;  /* Comparison function. */
};

/* Basic life cycle. */
void heap_init (struct heap *, heap_less_func *);

/* Operations. */
void heap_push (struct heap *, struct heap_elem *, void *aux);
struct heap_elem *heap_pop (struct heap *, void *aux);
/* WARNING: You are allowed to modify the data of the top element, but
   modifying the data in a way that affects the heap structure (e.g., changing
   a key used for ordering) will break the heap. */
struct heap_elem *heap_top (struct heap *);

/* Information. */
size_t heap_size (struct heap *);
bool heap_empty (struct heap *);

#endif /* lib/kernel/heap.h */
