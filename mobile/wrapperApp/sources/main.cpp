#include <stdlib.h>
#include <unistd.h>
#include <qqml.h>
#include <QDir>

extern "C" {
    void NimMain();
}

#ifdef Q_OS_ANDROID
#include <cstring>
#include <link.h>
#include <sys/mman.h>
#include <vector>

namespace {

struct PageRange { uintptr_t begin; uintptr_t end; };

int collectReadOnlyLibRange(dl_phdr_info *info, size_t, void *data)
{
    if (!info->dlpi_name || !strstr(info->dlpi_name, "/data/app/"))
        return 0;
    auto ranges = static_cast<std::vector<PageRange> *>(data);
    const uintptr_t pageSize = getpagesize();
    uintptr_t relroBegin = 0, relroEnd = 0;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const auto &ph = info->dlpi_phdr[i];
        if (ph.p_type == PT_GNU_RELRO) {
            relroBegin = info->dlpi_addr + ph.p_vaddr;
            relroEnd = relroBegin + ph.p_memsz;
        }
    }
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const auto &ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD || (ph.p_flags & PF_W))
            continue;
        uintptr_t begin = (info->dlpi_addr + ph.p_vaddr + pageSize - 1) & ~(pageSize - 1);
        uintptr_t end = (info->dlpi_addr + ph.p_vaddr + ph.p_filesz) & ~(pageSize - 1);
        if (relroEnd > begin && relroBegin < end)
            continue;
        if (end > begin)
            ranges->push_back({begin, end});
    }
    return 0;
}

// [not for merge] probe: STATUS_FAULT=random|random_keep|dontneed0, applied at main() entry.
// random: no fault-around/read-around from here on, and drop what the loader mapped, so
// residency afterwards ~= the pages actually touched (the floor a perfect code layout reaches).
void applyFaultProbe()
{
    const char *mode = getenv("STATUS_FAULT");
    if (!mode)
        return;
    const bool random = !strncmp(mode, "random", 6);
    const bool drop = strcmp(mode, "random_keep") != 0;
    std::vector<PageRange> ranges;
    dl_iterate_phdr(collectReadOnlyLibRange, &ranges);
    size_t bytes = 0;
    int failures = 0;
    for (const auto &r : ranges) {
        void *p = reinterpret_cast<void *>(r.begin);
        const size_t len = r.end - r.begin;
        if ((random && madvise(p, len, MADV_RANDOM) != 0) || (drop && madvise(p, len, MADV_DONTNEED) != 0))
            ++failures;
        else
            bytes += len;
    }
    qInfo("fault-probe: %s on %zu ranges, %zu MB, %d failed", mode, ranges.size(), bytes >> 20, failures);
}

} // namespace
#endif

int main(int argc, char* argv[])
{
#ifdef Q_OS_ANDROID
    applyFaultProbe();
#endif
    Q_INIT_RESOURCE(resources);

    // Must match the qmlcachegen shim style in Status.pro
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE"))
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

#ifdef Q_OS_ANDROID
    // MediaCodec backend; the ffmpeg one and its libav* libs are not packaged
    if (!qEnvironmentVariableIsSet("QT_MEDIA_BACKEND"))
        qputenv("QT_MEDIA_BACKEND", "android");
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    qmlRegisterModule("Qt.labs.settings", 1, 1);
    qmlRegisterModule("Qt.labs.settings", 1, 0);

    qmlRegisterModuleImport("Qt.labs.settings", QQmlModuleImportModuleAny,
                       "QtCore", QQmlModuleImportLatest);
#endif

    NimMain();
    return 0;
}
