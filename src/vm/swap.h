#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "vm/frame.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The swap partition is split into small slots. This is the unique identifier
   for each slot. */
typedef size_t slot_id;
#define SLOT_ERR SIZE_MAX

void swap_init (void);
slot_id swap_out (const void *kpage);
bool swap_in (slot_id swap_idx, void *kpage);

#endif /* vm/swap.h */
