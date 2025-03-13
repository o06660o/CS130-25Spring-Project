#include "heap.h"
#include "../debug.h"

static void heap_elem_swap (struct heap_elem **a, struct heap_elem **b);
struct heap_elem *merge (struct heap *heap, struct heap_elem *x);
struct heap_elem *meld (struct heap *heap, struct heap_elem *x,
                        struct heap_elem *y);

/* Initializes HEAP as an empty heap. */
void
heap_init (struct heap *heap, heap_less_func *less)
{
  ASSERT (heap != NULL);
  heap->less = less;
  heap->top = NULL;
  heap->size = 0;
}

/* Merge two heap into one. */
struct heap_elem *
meld (struct heap *heap, struct heap_elem *x, struct heap_elem *y)
{
  if (x == NULL)
    return y;
  if (y == NULL)
    return x;
  if (heap->less (x, y, NULL))
    heap_elem_swap (&x, &y);
  y->sibling = x->child;
  x->child = y;
  return x;
}

/* Merge all brother of HEAP_ELEM */
struct heap_elem *
merge (struct heap *heap, struct heap_elem *x)
{
  if (x == NULL || x->sibling == NULL)
    return x;
  struct heap_elem *y = x->sibling;
  struct heap_elem *a = y->sibling;
  x->sibling = y->sibling = NULL;
  return meld (heap, merge (heap, a), meld (heap, x, y));
}

/* Swap two HEAP_ELEM. */
static void
heap_elem_swap (struct heap_elem **a, struct heap_elem **b)
{
  struct heap_elem *tmp = *a;
  *a = *b;
  *b = tmp;
}

/* Push ELEM to the HEAP. */
void
heap_push (struct heap *heap, struct heap_elem *elem)
{
  ASSERT (heap != NULL);
  elem->child = elem->sibling = NULL;
  heap->top = meld (heap, heap->top, elem);
  ++heap->size;
}

/* Return the top element in HEAP and delete it. */
struct heap_elem *
heap_pop (struct heap *heap)
{
  ASSERT (heap != NULL);
  if (heap_empty (heap))
    return NULL;
  struct heap_elem *t = heap->top;
  heap->top = merge (heap, t->child);
  --heap->size;
  return t;
}

/* Return the top element in HEAP, which is the biggest one. */
struct heap_elem *
heap_top (struct heap *heap)
{
  ASSERT (heap != NULL);
  return heap->top;
}

/* Return the number of elements in HEAP. */
size_t
heap_size (struct heap *heap)
{
  ASSERT (heap != NULL);
  return heap->size;
}

/* Return true if HEAP is empty, false otherwise. */
bool
heap_empty (struct heap *heap)
{
  ASSERT (heap != NULL);
  return heap->size == 0;
}
