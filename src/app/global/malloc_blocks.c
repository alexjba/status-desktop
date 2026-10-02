/* Profiling builds: enumerate every live malloc block in-process, attach its MallocStackLogging stack
 * (launch with MallocStackLogging=lite), aggregate by the top frames and write "bytes count frame..." lines.
 * Symbolise offline with atos against the dSYM using the recorded slide. */
#include <malloc/malloc.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* private libmalloc entry point (stack_logging.h); not in the SDK stubs, so resolved at runtime */
typedef kern_return_t (*get_frames_fn)(task_t task, uint64_t address, uint64_t *stack_frames_buffer,
                                       uint32_t max_stack_frames, uint32_t *count);
static get_frames_fn get_frames;
static uint64_t img_lo, img_hi;   /* main image __TEXT..__DATA range: C++ vtables and Nim NTIv2 records live here */
static uint64_t size_hist[64][2]; /* blocks without any tag: count/bytes per power-of-two size class */
static void *msl_handle;

/* Stack logging itself is enabled by the launch environment (MallocStackLogging=lite); turning it on from
 * inside the process re-enters libMallocStackLogging once it is already on. Only resolve the frame lookup here. */
int status_malloc_logging_start(void) {
  if (!msl_handle) msl_handle = dlopen("/usr/lib/system/libMallocStackLogging.dylib", RTLD_NOW | RTLD_NOLOAD);
  get_frames = (get_frames_fn)dlsym(msl_handle ? msl_handle : RTLD_DEFAULT, "__mach_stack_logging_get_frames");
  if (!get_frames) get_frames = (get_frames_fn)dlsym(RTLD_DEFAULT, "__mach_stack_logging_get_frames");
  return get_frames ? 0 : -2;
}

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
    if (!get_frames || get_frames(mach_task_self(), ranges[i].address, fr, 64, &cnt) != KERN_SUCCESS || cnt == 0) {
      /* no stack: classify by the block's first word when it points into the app image (C++ vtable, Nim type info) */
      uint64_t w0 = ranges[i].size >= 8 ? *(uint64_t *)ranges[i].address : 0;
      if (w0 >= img_lo && w0 < img_hi) { fr[0] = w0 | (1ULL << 63); cnt = 1; }
      else {
        unsigned cls = 0; uint64_t sz = ranges[i].size; while (sz > 1 && cls < 63) { sz >>= 1; cls++; }
        size_hist[cls][0]++; size_hist[cls][1] += ranges[i].size;
        nostack_blocks++; nostack_bytes += ranges[i].size; continue;
      }
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
  memset(size_hist, 0, sizeof size_hist);
  {
    const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(0);
    unsigned long tsz = 0, dsz = 0;
    uint8_t *t = getsegmentdata(mh, "__TEXT", &tsz), *d = getsegmentdata(mh, "__DATA_CONST", &dsz);
    if (!d) d = getsegmentdata(mh, "__DATA", &dsz);
    img_lo = (uint64_t)(t ? t : (uint8_t *)mh); img_hi = d ? (uint64_t)d + dsz : img_lo + tsz;
  }
  if (!get_frames) status_malloc_logging_start();
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
            get_frames ? 1 : 0);
    for (unsigned c = 0; c < 64; c++) if (size_hist[c][0])
      fprintf(f, "%llu %llu size:%u\n", (unsigned long long)size_hist[c][1], (unsigned long long)size_hist[c][0], 1u << c);
    for (unsigned i = 0; i < NB; i++) if (buckets[i].count) {
      fprintf(f, "%llu %u", (unsigned long long)buckets[i].bytes, buckets[i].count);
      for (unsigned k = 0; k < buckets[i].nframes; k++) fprintf(f, " %llx", (unsigned long long)buckets[i].frames[k]);
      fputc('\n', f);
    }
    fclose(f);
  }
  munmap(buckets, sz); buckets = NULL;
}
