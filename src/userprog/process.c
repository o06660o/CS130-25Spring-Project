#include "userprog/process.h"
#include "devices/timer.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef VM
#include "userprog/syscall.h"
#include "vm/page.h"
#endif

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* A hash table to find exit data from tid. */
static struct hash hash_exit_data;

/* Helper function for hash table. */
static unsigned tid_hash (tid_t);
static unsigned hash_func (const struct hash_elem *, void *);
static bool hash_less (const struct hash_elem *, const struct hash_elem *,
                       void *);
static bool init_exit_data (struct thread *);
static void destroy_exit_data (struct exit_data *);

/* Initializes the process module. */
void
process_init (void)
{
  hash_init (&hash_exit_data, hash_func, hash_less, NULL);
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Make another copy of FN_COPY to get the correct program name.
     Otherwise it will be modified by strtok_r(). */
  char *fn_copy2 = palloc_get_page (0);
  if (fn_copy2 == NULL)
    return TID_ERROR;
  strlcpy (fn_copy2, fn_copy, PGSIZE);
  char *save_ptr;
  char *name = strtok_r (fn_copy2, " ", &save_ptr);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);
  palloc_free_page (fn_copy2);

  struct thread *new_thread = tid_to_thread (tid);
  struct thread *cur = thread_current ();
  ASSERT (new_thread->creator == cur);
#ifdef FILESYS
  new_thread->cwd = cur->cwd; /* Inherit the current working directory. */
#endif
  if (!init_exit_data (new_thread))
    return TID_ERROR;
  /* Don't use new_thread->creator to refer to cur, because new_thread may
     have already died. */
  sema_down (&cur->ch_load_sema);
  ASSERT (cur->ch_load_status != LOAD_READY);
  bool load_success = cur->ch_load_status == LOAD_SUCCESS;
  cur->ch_load_status = LOAD_READY;
  if (!load_success)
    return TID_ERROR;
  return tid;
}

/* Initializes the exit data of the thread T. */
static bool
init_exit_data (struct thread *t)
{
  struct exit_data *data = malloc (sizeof (struct exit_data));
  if (data == NULL)
    return false;
  data->tid = t->tid;
  ASSERT (t->creator != NULL);
  data->father = t->creator;
  data->called_process_wait = false;
  data->exit_code = -1;
  sema_init (&data->die_sema, 0);
  enum intr_level old_level = intr_disable ();
  hash_insert (&hash_exit_data, &data->hashelem);
  list_push_back (&t->creator->ch_exit_data, &data->listelem);
  intr_set_level (old_level);
  return true;
}

/* Destroy the exit data of the thread T. */
static void
destroy_exit_data (struct exit_data *data)
{
  enum intr_level old_level = intr_disable ();
  hash_delete (&hash_exit_data, &data->hashelem);
  intr_set_level (old_level);
  free (data);
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  ASSERT (strlen (file_name) + 1 < CMDLEN_MAX);
  struct intr_frame if_;
  bool success;

  /* Parse arguments. */
  int argc = 0;
  char *argv[ARGV_MAX + 1];
  for (char *save_ptr, *arg = strtok_r (file_name, " ", &save_ptr);
       arg != NULL && argc < ARGV_MAX; arg = strtok_r (NULL, " ", &save_ptr))
    argv[argc++] = arg;
  argv[argc] = NULL;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (argv[0], &if_.eip, &if_.esp);

  struct thread *cur = thread_current ();
  cur->creator->ch_load_status = success ? LOAD_SUCCESS : LOAD_FAIL;
  sema_up (&cur->creator->ch_load_sema);

  /* If load failed, quit. */
  if (!success)
    {
      palloc_free_page (file_name);
      thread_exit ();
    }

  /* Push arguments into user stack. */
  uint8_t *esp = if_.esp;
  esp = (uint8_t *)((uintptr_t)esp & ~3); /* Round down. */
  char *argv_ptrs[argc + 1];
  for (int i = argc - 1; i >= 0; i--) /* argv[i][...]. */
    {
      size_t len = strlen (argv[i]) + 1;
      esp -= len;
      memcpy (esp, argv[i], len);
      argv_ptrs[i] = (char *)esp;
    }
  argv_ptrs[argc] = NULL;
  esp = (uint8_t *)((uintptr_t)esp & ~3); /* Round again. */
  for (int i = argc; i >= 0; i--)         /* argv[i]. */
    {
      esp -= sizeof (char *);
      *(char **)esp = argv_ptrs[i];
    }
  char **argv0 = (char **)esp; /* argv. */
  esp -= sizeof (char **);
  *(char ***)esp = argv0;
  esp -= sizeof (int); /* argc. */
  *(int *)esp = argc;
  esp -= sizeof (void *); /* Return address. */
  *(void **)esp = NULL;
#ifdef DEBUG
  size_t buf_size = (uint8_t *)if_.esp - esp;
  hex_dump ((uintptr_t)esp, esp, buf_size, true);
#endif
  if_.esp = esp;

  palloc_free_page (file_name);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting. */
int
process_wait (tid_t child_tid)
{
  struct exit_data *data = tid_to_exit_data (child_tid);
  if (data == NULL)
    return -1; /* Invalid TID. */
  if (data->father == NULL || data->father->tid != thread_current ()->tid)
    return -1; /* Not a child. */
  if (data->called_process_wait)
    return -1; /* Already waited. */
  data->called_process_wait = true;
  sema_down (&data->die_sema); /* Wait for the child to die. */
  return data->exit_code;
}

/* Free the current process's resources and print terminate message. */
void
process_exit (int status)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Process termination message. */
  printf ("%s: exit(%d)\n", cur->name, status);

  file_close (cur->exec_file);

  struct exit_data *data = tid_to_exit_data (cur->tid);
  /* If data is NULL, it means that the parent has already exited. */
  if (data != NULL)
    {
      data->exit_code = status;
      sema_up (&data->die_sema);
    }

  /* Clear the children's exit data. */
  struct list_elem *st = list_begin (&cur->ch_exit_data);
  struct list_elem *ed = list_end (&cur->ch_exit_data);
  for (struct list_elem *it = st; it != ed;)
    {
      struct exit_data *data = list_entry (it, struct exit_data, listelem);
      it = list_next (it);
      ASSERT (data != NULL);
      destroy_exit_data (data);
    }

#ifdef VM
  /* Clear the supplemental pages this process owns. */
  st = list_begin (&cur->page_list);
  ed = list_end (&cur->page_list);
  for (struct list_elem *it = st; it != ed;)
    {
      struct page *page = list_entry (it, struct page, listelem);
      it = list_next (it);
      ASSERT (page != NULL);
      page_free (page);
    }

  /* Clear mmaped files. */
  while (cur->mapid_next)
    syscall_munmap (--cur->mapid_next);
#endif

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  /* In pintos, every process only own one thread. */
  thread_exit ();
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2
      || ehdr.e_machine != 3 || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr) || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *)mem_page, read_bytes,
                                 zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void))ehdr.e_entry;

  success = true;

done:
  /* We arrive here whether the load is successful or not. */
  if (success)
    {
      file_deny_write (file);
      t->exec_file = file;
    }
  else
    file_close (file);

  return success;
}

#ifndef VM
/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);
#endif

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

#ifdef VM
      /* Lazy load this page. */
      if (!page_lazy_load (file, ofs, upage, page_read_bytes, page_zero_bytes,
                           writable, PAGE_UNALLOC))
        return false;
#else
      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int)page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }
#endif

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp)
{
  bool success = false;

#ifdef VM
  success = page_full_load_stack ((uint8_t *)PHYS_BASE - PGSIZE);
  if (success)
    {
      *esp = PHYS_BASE;
      thread_current ()->user_esp = PHYS_BASE;
    }
#else
  uint8_t *kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *)PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
#endif
  return success;
}

#ifndef VM
/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  return pagedir_install_page (thread_current (), upage, kpage, writable);
}
#endif

/* Find the exit data by TID. */
struct exit_data *
tid_to_exit_data (tid_t tid)
{
  struct exit_data tmp;
  tmp.tid = tid;
  struct hash_elem *elem = hash_find (&hash_exit_data, &tmp.hashelem);
  return elem != NULL ? hash_entry (elem, struct exit_data, hashelem) : NULL;
}

/* Helper function for hash table. */
static unsigned
tid_hash (tid_t tid)
{
  return tid;
}

/* Helper function for hash table. */
static unsigned
hash_func (const struct hash_elem *elem, void *aux UNUSED)
{
  struct exit_data *data = hash_entry (elem, struct exit_data, hashelem);
  return tid_hash (data->tid);
}

/* Helper function for hash table. */
static bool
hash_less (const struct hash_elem *lhs, const struct hash_elem *rhs,
           void *aux UNUSED)
{
  struct exit_data *lhs_ = hash_entry (lhs, struct exit_data, hashelem);
  struct exit_data *rhs_ = hash_entry (rhs, struct exit_data, hashelem);
  return lhs_->tid < rhs_->tid;
}
