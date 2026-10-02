/* Profiling builds: enumerate every live malloc block in-process, attach its MallocStackLogging stack
 * (launch with MallocStackLogging=lite), aggregate by the top frames and write "bytes count frame..." lines.
 * Symbolise offline with atos against the dSYM using the recorded slide. */
#include <malloc/malloc.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern kern_return_t __mach_stack_logging_get_frames(task_t task, uint64_t address, uint64_t *stack_frames_buffer,
                                                     uint32_t max_stack_frames, uint32_t *count) __attribute__((weak_import));

#define NB (1u << 18)
#define NF 10
typedef struct { uint64_t frames[NF]; uint64_t bytes; uint32_t count; uint32_t nframes; } bucket_t;
static bucket_t *buckets;
static uint64_t nblocks, nbytes, nostack_blocks, nostack_bytes, dropped;

static kern_return_t rd(task_t t, vm_address_t a, vm_size_t s, void **local) { (void)t; (void)s; *local = (void *)a; return KERN_SUCCESS; }
static void rec(task_t t, void *ctx, unsigned type, vm_range_t *ranges, unsigned n) {
  (void)t; (void)ctx; (void)type;
  for (unsigned i = 0; i < n; i++) {
    uint64_t fr[64]; uint32_t cnt = 0;
    nblocks++; nbytes += ranges[i].size;
    if (!__mach_stack_logging_get_frames ||
        __mach_stack_logging_get_frames(mach_task_self(), ranges[i].address, fr, 64, &cnt) != KERN_SUCCESS || cnt == 0) {
      nostack_blocks++; nostack_bytes += ranges[i].size; continue;
    }
    uint32_t keep = cnt < NF ? cnt : NF; uint64_t h = 1469598103934665603ULL;
    for (uint32_t k = 0; k < keep; k++) { h ^= fr[k]; h *= 1099511628211ULL; }
    unsigned probe;
    for (probe = 0; probe < 64; probe++) {
      bucket_t *b = &buckets[(h + probe) & (NB - 1)];
      if (b->count == 0) { memcpy(b->frames, fr, keep * sizeof(uint64_t)); b->nframes = keep; b->count = 1; b->bytes = ranges[i].size; break; }
      if (b->nframes == keep && memcmp(b->frames, fr, keep * sizeof(uint64_t)) == 0) { b->count++; b->bytes += ranges[i].size; break; }
    }
    if (probe == 64) dropped++;
  }
}

void status_malloc_blocks(const char *path) {
  size_t sz = (size_t)NB * sizeof(bucket_t);
  buckets = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  if (buckets == MAP_FAILED) return;
  nblocks = nbytes = nostack_blocks = nostack_bytes = dropped = 0;
  vm_address_t *zones = NULL; unsigned nz = 0;
  if (malloc_get_all_zones(mach_task_self(), NULL, &zones, &nz) == KERN_SUCCESS)
    for (unsigned i = 0; i < nz; i++) {
      malloc_zone_t *z = (malloc_zone_t *)zones[i];
      if (z->introspect && z->introspect->enumerator)
        z->introspect->enumerator(mach_task_self(), NULL, MALLOC_PTR_IN_USE_RANGE_TYPE, (vm_address_t)z, rd, rec);
    }
  FILE *f = fopen(path, "w");
  if (f) {
    fprintf(f, "# blocks=%llu bytes=%llu nostack_blocks=%llu nostack_bytes=%llu dropped=%llu slide=0x%lx stacklogging=%d\n",
            (unsigned long long)nblocks, (unsigned long long)nbytes, (unsigned long long)nostack_blocks,
            (unsigned long long)nostack_bytes, (unsigned long long)dropped, (unsigned long)_dyld_get_image_vmaddr_slide(0),
            __mach_stack_logging_get_frames ? 1 : 0);
    for (unsigned i = 0; i < NB; i++) if (buckets[i].count) {
      fprintf(f, "%llu %u", (unsigned long long)buckets[i].bytes, buckets[i].count);
      for (unsigned k = 0; k < buckets[i].nframes; k++) fprintf(f, " %llx", (unsigned long long)buckets[i].frames[k]);
      fputc('\n', f);
    }
    fclose(f);
  }
  munmap(buckets, sz); buckets = NULL;
}
