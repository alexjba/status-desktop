#include <stdlib.h>
#include <unistd.h>
#include <qqml.h>
#include <QDir>

#ifdef Q_OS_ANDROID
#include <chrono>
#include <cstring>
#include <link.h>
#include <sys/mman.h>
#include <thread>
#include <vector>
#include <QElapsedTimer>
#endif

extern "C" {
    void NimMain();
}

#ifdef Q_OS_ANDROID
namespace {

#ifndef MADV_COLD
#define MADV_COLD 20
#endif
#ifndef MADV_PAGEOUT
#define MADV_PAGEOUT 21
#endif

struct PageRange { uintptr_t begin; uintptr_t end; };

// Read-only PT_LOAD segments (r--p, r-xp) of the libraries shipped in our APK. These are
// never written (no text relocations, RELRO excluded), so their pages are clean file pages
// that refault from the page cache.
int collectRange(dl_phdr_info *info, size_t, void *data)
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
        uintptr_t begin = info->dlpi_addr + ph.p_vaddr;
        uintptr_t end = begin + ph.p_filesz;
        begin = (begin + pageSize - 1) & ~(pageSize - 1);
        end &= ~(pageSize - 1);
        if (relroEnd > begin && relroBegin < end)
            continue;
        if (end > begin)
            ranges->push_back({begin, end});
    }
    return 0;
}

int adviceFromEnv(const char *value)
{
    if (!value || !strcmp(value, "dontneed"))
        return MADV_DONTNEED;
    if (!strcmp(value, "pageout"))
        return MADV_PAGEOUT;
    if (!strcmp(value, "cold"))
        return MADV_COLD;
    return -1;
}

void releaseLibraryPages(int advice)
{
    QElapsedTimer timer;
    timer.start();
    std::vector<PageRange> ranges;
    dl_iterate_phdr(collectRange, &ranges);
    size_t bytes = 0;
    int failures = 0;
    for (const auto &r : ranges) {
        if (madvise(reinterpret_cast<void *>(r.begin), r.end - r.begin, advice) == 0)
            bytes += r.end - r.begin;
        else
            ++failures;
    }
    qInfo("pages: advice %d on %zu ranges, %zu MB, %d failed, %lld us", advice, ranges.size(),
          bytes >> 20, failures, static_cast<long long>(timer.nsecsElapsed() / 1000));
}

// STATUS_MADV=dontneed|pageout|cold, STATUS_MADV_AT=<seconds after start>[,<seconds>...]
void scheduleLibraryPageRelease()
{
    const char *at = getenv("STATUS_MADV_AT");
    const int advice = adviceFromEnv(getenv("STATUS_MADV"));
    if (!at || advice < 0)
        return;
    std::vector<long> seconds;
    for (const char *p = at; *p;) {
        char *next = nullptr;
        const long s = strtol(p, &next, 10);
        if (next == p)
            break;
        seconds.push_back(s);
        p = (*next == ',') ? next + 1 : next;
    }
    std::thread([advice, seconds] {
        const auto start = std::chrono::steady_clock::now();
        for (long s : seconds) {
            std::this_thread::sleep_until(start + std::chrono::seconds(s));
            releaseLibraryPages(advice);
        }
    }).detach();
}

} // namespace
#endif

int main(int argc, char* argv[])
{
    Q_INIT_RESOURCE(resources);

#ifdef Q_OS_ANDROID
    scheduleLibraryPageRelease();
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
