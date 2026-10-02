/* Profiling builds, opt-in through the launch environment (STATUS_MALLOC_HOOK=1): install ourselves as libmalloc's
 * malloc_logger and keep (pointer -> uniqued backtrace, size) for every live block allocated after start-up, in
 * mmap'd tables (never malloc inside the hook). status_malloc_hook_dump() writes "bytes count frame..." lines
 * aggregated by stack, the same format as malloc_blocks.c, for mallocsym.py + the dSYM. */
#include <dlfcn.h>
#include <execinfo.h>
#include <os/lock.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <mach-o/dyld.h>

typedef void (malloc_logger_t)(uint32_t type, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t result,
                               uint32_t num_hot_frames_to_skip);
enum { LOG_ALLOC = 2, LOG_DEALLOC = 4, LOG_FLAG_ZONE = 8, LOG_VM = 16 | 32 };

#define PTR_SLOTS (1u << 22) /* 4M live pointers, 64MB */
#define STK_SLOTS (1u << 18) /* 256k unique stacks, 32MB */
#define STK_DEPTH 14
typedef struct { uintptr_t ptr; uint32_t stack; uint32_t size; } ptr_ent;
typedef struct { uint64_t hash; uint32_t depth; uint32_t used; uintptr_t frames[STK_DEPTH]; } stk_ent;
static ptr_ent *ptrs; static stk_ent *stks;
static os_unfair_lock lk = OS_UNFAIR_LOCK_INIT;
static _Thread_local int in_hook;
static uint64_t n_live, dropped_ptrs, dropped_stks, hook_active;

static uint32_t stack_id(uint32_t skip) {
  void *buf[STK_DEPTH + 8]; int n = backtrace(buf, STK_DEPTH + 8);
  int start = 2 + (int)skip; if (start >= n) start = n > 0 ? n - 1 : 0;
  uint32_t depth = (uint32_t)(n - start); if (depth > STK_DEPTH) depth = STK_DEPTH;
  uint64_t h = 1469598103934665603ULL;
  for (uint32_t k = 0; k < depth; k++) { h ^= (uintptr_t)buf[start + k]; h *= 1099511628211ULL; }
  for (unsigned probe = 0; probe < 128; probe++) {
    stk_ent *e = &stks[(h + probe) & (STK_SLOTS - 1)];
    if (!e->used) { e->used = 1; e->hash = h; e->depth = depth; memcpy(e->frames, buf + start, depth * sizeof(uintptr_t)); return (uint32_t)(e - stks) + 1; }
    if (e->hash == h && e->depth == depth && memcmp(e->frames, buf + start, depth * sizeof(uintptr_t)) == 0) return (uint32_t)(e - stks) + 1;
  }
  dropped_stks++; return 0;
}
static inline uint64_t pslot(uintptr_t p) { return ((p >> 4) * 0x9E3779B97F4A7C15ULL) & (PTR_SLOTS - 1); }
static void record(uintptr_t p, size_t size, uint32_t skip) {
  uint32_t id = stack_id(skip); if (!id) return;
  uint64_t s = pslot(p);
  for (unsigned probe = 0; probe < 256; probe++) {
    ptr_ent *e = &ptrs[(s + probe) & (PTR_SLOTS - 1)];
    if (e->ptr == 0 || e->ptr == 1 || e->ptr == p) { if (e->ptr != p) n_live++; e->ptr = p; e->stack = id; e->size = size > 0xffffffffu ? 0xffffffffu : (uint32_t)size; return; }
  }
  dropped_ptrs++;
}
static void forget(uintptr_t p) {
  if (!p) return;
  uint64_t s = pslot(p);
  for (unsigned probe = 0; probe < 256; probe++) {
    ptr_ent *e = &ptrs[(s + probe) & (PTR_SLOTS - 1)];
    if (e->ptr == 0) return;
    if (e->ptr == p) { e->ptr = 1; n_live--; return; }
  }
}
static void hook(uint32_t type, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t result, uint32_t skip) {
  (void)a1;
  if (in_hook || (type & LOG_VM)) return;
  in_hook = 1;
  uint32_t t = type & ~(uint32_t)LOG_FLAG_ZONE;
  os_unfair_lock_lock(&lk);
  if (t == (LOG_ALLOC | LOG_DEALLOC)) { forget(a2); if (result) record(result, a3, skip); } /* realloc: old, size */
  else if (t == LOG_ALLOC) { if (result) record(result, a2, skip); }                            /* size */
  else if (t == LOG_DEALLOC) forget(a2);                                                        /* ptr */
  os_unfair_lock_unlock(&lk);
  in_hook = 0;
}

int status_malloc_hook_start(void) {
  const char *on = getenv("STATUS_MALLOC_HOOK");
  if (!on || !*on || *on == '0') return 1;
  malloc_logger_t **slot = (malloc_logger_t **)dlsym(RTLD_DEFAULT, "malloc_logger");
  if (!slot) return -1;
  ptrs = mmap(NULL, (size_t)PTR_SLOTS * sizeof(ptr_ent), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  stks = mmap(NULL, (size_t)STK_SLOTS * sizeof(stk_ent), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  if (ptrs == MAP_FAILED || stks == MAP_FAILED) return -3;
  *slot = hook; hook_active = 1;
  return 0;
}

int status_malloc_hook_active(void) { return (int)hook_active; }

/* Aggregate the live table by stack. Buckets live in the (already allocated) stack table: reuse hash/depth, sum into
 * per-stack counters kept in a side array. */
void status_malloc_hook_dump(const char *path) {
  if (!hook_active) return;
  size_t asz = (size_t)STK_SLOTS * 2 * sizeof(uint64_t);
  uint64_t *acc = mmap(NULL, asz, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  if (acc == MAP_FAILED) return;
  in_hook = 1; os_unfair_lock_lock(&lk);
  uint64_t live = 0, bytes = 0;
  for (uint64_t i = 0; i < PTR_SLOTS; i++) {
    ptr_ent *e = &ptrs[i];
    if (e->ptr > 1 && e->stack) { acc[(e->stack - 1) * 2] += e->size; acc[(e->stack - 1) * 2 + 1]++; live++; bytes += e->size; }
  }
  os_unfair_lock_unlock(&lk); in_hook = 0;
  FILE *f = fopen(path, "w");
  if (f) {
    fprintf(f, "# blocks=%llu bytes=%llu nostack_blocks=0 nostack_bytes=0 dropped=%llu dropped_stacks=%llu slide=0x%lx stacklogging=hook\n",
            (unsigned long long)live, (unsigned long long)bytes, (unsigned long long)dropped_ptrs, (unsigned long long)dropped_stks,
            (unsigned long)_dyld_get_image_vmaddr_slide(0));
    for (uint64_t i = 0; i < STK_SLOTS; i++) if (acc[i * 2 + 1]) {
      fprintf(f, "%llu %llu", (unsigned long long)acc[i * 2], (unsigned long long)acc[i * 2 + 1]);
      for (uint32_t k = 0; k < stks[i].depth; k++) fprintf(f, " %llx", (unsigned long long)stks[i].frames[k]);
      fputc('\n', f);
    }
    fclose(f);
  }
  munmap(acc, asz);
}
