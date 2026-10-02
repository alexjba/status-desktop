/* Profiling builds (-d:goPprof): write a vmmap-style summary of this process — footprint, VM regions
 * aggregated by user tag, malloc zone statistics — so a device without host-tool access can be sampled. */
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <malloc/malloc.h>
#include <stdio.h>
#include <string.h>

static const char *status_vm_tag_name(unsigned t) {
  switch (t) {
    case 0: return "untagged(VM_ALLOCATE)"; case 1: return "MALLOC"; case 2: return "MALLOC_SMALL"; case 3: return "MALLOC_LARGE";
    case 4: return "MALLOC_HUGE"; case 6: return "REALLOC"; case 7: return "MALLOC_TINY"; case 8: return "MALLOC_LARGE_REUSABLE";
    case 9: return "MALLOC_LARGE_REUSED"; case 10: return "ANALYSIS_TOOL"; case 11: return "MALLOC_NANO"; case 12: return "MALLOC_MEDIUM";
    case 20: return "MACH_MSG"; case 21: return "IOKIT"; case 30: return "STACK"; case 31: return "GUARD"; case 32: return "SHARED_PMAP";
    case 33: return "DYLIB"; case 34: return "OBJC_DISPATCHERS"; case 35: return "UNSHARED_PMAP"; case 41: return "FOUNDATION";
    case 42: return "COREGRAPHICS"; case 43: return "CORESERVICES"; case 48: return "LAYERKIT(CoreAnimation)"; case 49: return "CGIMAGE";
    case 51: return "COREGRAPHICS_DATA"; case 53: return "COREGRAPHICS_FRAMEBUFFERS"; case 54: return "COREGRAPHICS_BACKINGSTORES";
    case 60: return "DYLD"; case 61: return "DYLD_MALLOC"; case 62: return "SQLITE"; case 63: return "JAVASCRIPT_CORE";
    case 64: return "JS_JIT_EXECUTABLE"; case 68: return "COREIMAGE"; case 70: return "IMAGEIO"; case 73: return "OS_ALLOC_ONCE";
    case 74: return "LIBDISPATCH"; case 75: return "ACCELERATE"; case 76: return "COREUI"; case 77: return "COREUIFILE";
    case 82: return "SWIFT_RUNTIME"; case 83: return "SWIFT_METADATA"; case 88: return "IOSURFACE"; case 89: return "LIBNETWORK";
    case 90: return "AUDIO"; case 100: return "IOACCELERATOR(GPU)"; case 103: return "COREUI_CACHED_IMAGE_DATA"; case 104: return "COLORSYNC";
    default: return NULL;
  }
}

void status_vm_summary(const char *path) {
  FILE *f = fopen(path, "w");
  if (!f) return;
  task_vm_info_data_t vi; mach_msg_type_number_t c = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &c) == KERN_SUCCESS)
    fprintf(f, "phys_footprint=%llu resident=%llu internal=%llu external=%llu compressed=%llu purgeable_volatile=%llu\n",
            (unsigned long long)vi.phys_footprint, (unsigned long long)vi.resident_size, (unsigned long long)vi.internal,
            (unsigned long long)vi.external, (unsigned long long)vi.compressed, (unsigned long long)vi.purgeable_volatile_resident);
  unsigned long long resident[256] = {0}, dirty[256] = {0}, swapped[256] = {0}, vsize[256] = {0}, regions[256] = {0};
  unsigned long long file_resident = 0, file_dirty = 0, file_regions = 0, file_vsize = 0;
  vm_address_t addr = 0; vm_size_t size = 0; natural_t depth = 0;
  unsigned long long pg = (unsigned long long)vm_kernel_page_size;
  for (;;) {
    vm_region_submap_info_data_64_t info; mach_msg_type_number_t ic = VM_REGION_SUBMAP_INFO_COUNT_64;
    if (vm_region_recurse_64(mach_task_self(), &addr, &size, &depth, (vm_region_recurse_info_t)&info, &ic) != KERN_SUCCESS) break;
    if (info.is_submap) { depth++; continue; }
    if (info.external_pager) {
      file_resident += (unsigned long long)info.pages_resident * pg; file_dirty += (unsigned long long)info.pages_dirtied * pg;
      file_regions++; file_vsize += size;
    } else {
      unsigned t = info.user_tag & 0xff;
      resident[t] += (unsigned long long)info.pages_resident * pg; dirty[t] += (unsigned long long)info.pages_dirtied * pg;
      swapped[t] += (unsigned long long)info.pages_swapped_out * pg; vsize[t] += size; regions[t]++;
    }
    addr += size;
  }
  fprintf(f, "tag,name,regions,virtual,resident,dirty,swapped\n");
  for (unsigned t = 0; t < 256; t++) {
    if (!regions[t]) continue;
    const char *n = status_vm_tag_name(t);
    fprintf(f, "%u,%s,%llu,%llu,%llu,%llu,%llu\n", t, n ? n : "tag", regions[t], vsize[t], resident[t], dirty[t], swapped[t]);
  }
  fprintf(f, "file,mapped file,%llu,%llu,%llu,%llu,0\n", file_regions, file_vsize, file_resident, file_dirty);
  vm_address_t *zones = NULL; unsigned nz = 0;
  if (malloc_get_all_zones(mach_task_self(), NULL, &zones, &nz) == KERN_SUCCESS) {
    fprintf(f, "zone,name,blocks_in_use,size_in_use,max_size_in_use,size_allocated\n");
    for (unsigned i = 0; i < nz; i++) {
      malloc_zone_t *z = (malloc_zone_t *)zones[i]; malloc_statistics_t st; memset(&st, 0, sizeof st);
      malloc_zone_statistics(z, &st);
      const char *n = malloc_get_zone_name(z);
      fprintf(f, "%u,%s,%u,%zu,%zu,%zu\n", i, n ? n : "?", st.blocks_in_use, st.size_in_use, st.max_size_in_use, st.size_allocated);
    }
  }
  fclose(f);
}
