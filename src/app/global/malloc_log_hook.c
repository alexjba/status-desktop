/* Profiling builds, opt-in through the launch environment (STATUS_MALLOC_HOOK=1): install ourselves as libmalloc's
 * malloc_logger and keep (pointer -> uniqued backtrace, size) for every live block allocated after start-up, in
 * mmap'd tables (never malloc inside the hook). status_malloc_hook_dump() writes "bytes count frame..." lines
 * aggregated by stack, the same format as malloc_blocks.c, for mallocsym.py + the dSYM. */
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
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
static atomic_flag lk = ATOMIC_FLAG_INIT;   /* plain spinlock: no library code, no misuse traps */
static pthread_key_t guard_key;             /* per-thread re-entrancy guard without TLS first-touch allocation */
static inline void lock(void) { while (atomic_flag_test_and_set_explicit(&lk, memory_order_acquire)) {} }
static inline void unlock(void) { atomic_flag_clear_explicit(&lk, memory_order_release); }
static inline int guard_enter(void) { if (pthread_getspecific(guard_key)) return 0; pthread_setspecific(guard_key, (void *)1); return 1; }
static inline void guard_leave(void) { pthread_setspecific(guard_key, NULL); }

/* arm64 frame-pointer walk: async-safe and allocation-free, which the libc unwinder is not inside a malloc hook */
static int walk(void **buf, int max) {
  uintptr_t fp = (uintptr_t)__builtin_frame_address(0);
  uintptr_t hi = (uintptr_t)pthread_get_stackaddr_np(pthread_self());
  uintptr_t lo = hi - pthread_get_stacksize_np(pthread_self());
  int n = 0;
  while (n < max && fp >= lo && fp + 16 <= hi && (fp & 0xf) == 0) {
    uintptr_t lr = ((uintptr_t *)fp)[1], next = ((uintptr_t *)fp)[0];
    if (!lr) break;
    buf[n++] = (void *)(lr & 0x0000007fffffffffULL); /* strip PAC bits */
    if (next <= fp) break;
    fp = next;
  }
  return n;
}
static uint64_t n_live, dropped_ptrs, dropped_stks, hook_active;
static uint64_t calls_by_type[256], guard_rejected;

static uint32_t stack_id(uint32_t skip) {
  void *buf[STK_DEPTH + 8]; int n = walk(buf, STK_DEPTH + 8);
  int start = 1 + (int)skip; if (start >= n) start = n > 0 ? n - 1 : 0;
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
  calls_by_type[type & 0xff]++;
  if (!(type & LOG_FLAG_ZONE)) return;      /* only malloc-zone events; VM events have no zone flag */
  if (!guard_enter()) { guard_rejected++; return; }
  int al = (type & LOG_ALLOC) != 0, de = (type & LOG_DEALLOC) != 0;
  lock();
  if (al && de) { forget(a2); if (result) record(result, a3, skip); } /* realloc: zone, old, size -> new */
  else if (al) { if (result) record(result, a2, skip); }              /* malloc/calloc/memalign: zone, size -> ptr */
  else if (de) forget(a2);                                            /* free: zone, ptr */
  unlock();
  guard_leave();
}

int status_malloc_hook_start(void) {
  const char *on = getenv("STATUS_MALLOC_HOOK");
  if (!on || !*on || *on == '0') return 1;
  malloc_logger_t **slot = (malloc_logger_t **)dlsym(RTLD_DEFAULT, "malloc_logger");
  if (!slot) return -1;
  ptrs = mmap(NULL, (size_t)PTR_SLOTS * sizeof(ptr_ent), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  stks = mmap(NULL, (size_t)STK_SLOTS * sizeof(stk_ent), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  if (ptrs == MAP_FAILED || stks == MAP_FAILED) return -3;
  if (pthread_key_create(&guard_key, NULL) != 0) return -4;
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
  guard_enter(); lock();
  uint64_t live = 0, bytes = 0;
  for (uint64_t i = 0; i < PTR_SLOTS; i++) {
    ptr_ent *e = &ptrs[i];
    if (e->ptr > 1 && e->stack) { acc[(e->stack - 1) * 2] += e->size; acc[(e->stack - 1) * 2 + 1]++; live++; bytes += e->size; }
  }
  unlock(); guard_leave();
  FILE *f = fopen(path, "w");
  if (f) {
    fprintf(f, "# blocks=%llu bytes=%llu nostack_blocks=0 nostack_bytes=0 dropped=%llu dropped_stacks=%llu slide=0x%lx stacklogging=hook guard_rejected=%llu types=",
            (unsigned long long)live, (unsigned long long)bytes, (unsigned long long)dropped_ptrs, (unsigned long long)dropped_stks,
            (unsigned long)_dyld_get_image_vmaddr_slide(0), (unsigned long long)guard_rejected);
    for (unsigned t = 0; t < 256; t++) if (calls_by_type[t]) fprintf(f, "%u:%llu,", t, (unsigned long long)calls_by_type[t]);
    fputc('\n', f);
    for (uint64_t i = 0; i < STK_SLOTS; i++) if (acc[i * 2 + 1]) {
      fprintf(f, "%llu %llu", (unsigned long long)acc[i * 2], (unsigned long long)acc[i * 2 + 1]);
      for (uint32_t k = 0; k < stks[i].depth; k++) fprintf(f, " %llx", (unsigned long long)stks[i].frames[k]);
      fputc('\n', f);
    }
    fclose(f);
  }
  munmap(acc, asz);
}
