#include <stdlib.h>
#include <unistd.h>
#include <qqml.h>
#include <QDir>

#ifdef Q_OS_ANDROID
#include <dirent.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <QtGlobal>

// [not for merge] page probe: STATUS_PAGEPROBE=split|drop,split (status-env.txt).
// split: every other page of our read-only library segments gets MADV_RANDOM, so each page is its
// own VMA and fault-around (which never crosses a VMA) maps exactly the touched pages.
// drop: MADV_DONTNEED those segments first (pre-main residency is dumped to pm-premain.bin before).
// Libraries loaded later are split (not dropped) within 1 s. `adb shell touch <files>/pmreq-<tag>`
// makes the probe write <files>/pm-<tag>.bin: merged base.apk read-only VMAs + their pagemap words.
namespace pageprobe {

struct Range { uintptr_t begin; uintptr_t end; };
const uintptr_t kPage = sysconf(_SC_PAGESIZE);

int collect(dl_phdr_info *info, size_t, void *data)
{
    if (!info->dlpi_name || !strstr(info->dlpi_name, "/data/app/") || !strstr(info->dlpi_name, "app.status."))
        return 0;
    auto ranges = static_cast<std::vector<Range> *>(data);
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
        uintptr_t begin = (info->dlpi_addr + ph.p_vaddr + kPage - 1) & ~(kPage - 1);
        uintptr_t end = (info->dlpi_addr + ph.p_vaddr + ph.p_filesz) & ~(kPage - 1);
        if (relroEnd > begin && relroBegin < end)
            continue;
        if (end > begin)
            ranges->push_back({begin, end});
    }
    return 0;
}

std::vector<Range> libraryRanges()
{
    std::vector<Range> ranges;
    dl_iterate_phdr(collect, &ranges);
    return ranges;
}

int split(const Range &r, bool drop)
{
    int failures = 0;
    if (drop && madvise(reinterpret_cast<void *>(r.begin), r.end - r.begin, MADV_DONTNEED) != 0)
        ++failures;
    for (uintptr_t p = r.begin + kPage; p < r.end; p += 2 * kPage)
        if (madvise(reinterpret_cast<void *>(p), kPage, MADV_RANDOM) != 0)
            ++failures;
    return failures;
}

std::string filesDir()
{
    char cmd[256] = {};
    FILE *f = fopen("/proc/self/cmdline", "r");
    if (f) {
        fread(cmd, 1, sizeof(cmd) - 1, f);
        fclose(f);
    }
    return std::string("/storage/emulated/0/Android/data/") + cmd + "/files";
}

int dump(const std::string &path)
{
    FILE *maps = fopen("/proc/self/maps", "r");
    int pm = open("/proc/self/pagemap", O_RDONLY);
    FILE *out = fopen((path + ".tmp").c_str(), "w");
    if (!maps || pm < 0 || !out)
        return -1;
    char line[1024];
    uintptr_t cs = 0, ce = 0, coff = 0;
    char cperm[8] = {};
    int vmas = 0;
    auto flush = [&]() {
        if (ce <= cs)
            return;
        const size_t n = (ce - cs) / kPage;
        std::vector<uint64_t> words(n);
        pread(pm, words.data(), n * 8, (cs / kPage) * 8);
        fprintf(out, "P %lx %lx %s %lx %zu\n", (unsigned long)cs, (unsigned long)ce, cperm, (unsigned long)coff, n * 8);
        fwrite(words.data(), 8, n, out);
    };
    while (fgets(line, sizeof(line), maps)) {
        unsigned long s, e, off;
        char perm[8], file[512] = {};
        if (sscanf(line, "%lx-%lx %7s %lx %*s %*s %511s", &s, &e, perm, &off, file) < 4)
            continue;
        ++vmas;
        const size_t fl = strlen(file);
        if (fl < 8 || strcmp(file + fl - 8, "base.apk") || strchr(perm, 'w'))
            continue;
        if (s == ce && !strcmp(perm, cperm) && off == coff + (ce - cs)) {
            ce = e;
            continue;
        }
        flush();
        cs = s; ce = e; coff = off;
        strcpy(cperm, perm);
    }
    flush();
    fclose(out);
    fclose(maps);
    close(pm);
    rename((path + ".tmp").c_str(), path.c_str());
    return vmas;
}

void start()
{
    const char *env = getenv("STATUS_PAGEPROBE");
    if (!env)
        return;
    const bool drop = strstr(env, "drop");
    const std::string dir = filesDir();
    qInfo("pageprobe: premain dump, %d vmas", dump(dir + "/pm-premain.bin"));
    std::set<uintptr_t> seen;
    int failures = 0;
    size_t bytes = 0;
    for (const auto &r : libraryRanges()) {
        failures += split(r, drop);
        bytes += r.end - r.begin;
        seen.insert(r.begin);
    }
    qInfo("pageprobe: %s on %zu ranges, %zu MB, %d failed", env, seen.size(), bytes >> 20, failures);
    std::thread([dir, seen]() mutable {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            int added = 0, failures = 0;
            for (const auto &r : libraryRanges())
                if (seen.insert(r.begin).second) {
                    failures += split(r, false);
                    ++added;
                }
            if (added)
                qInfo("pageprobe: split %d late ranges, %d failed", added, failures);
            DIR *d = opendir(dir.c_str());
            if (!d)
                continue;
            std::vector<std::string> reqs;
            while (dirent *e = readdir(d))
                if (!strncmp(e->d_name, "pmreq-", 6))
                    reqs.push_back(e->d_name + 6);
            closedir(d);
            for (const auto &tag : reqs) {
                const int vmas = dump(dir + "/pm-" + tag + ".bin");
                unlink((dir + "/pmreq-" + tag).c_str());
                qInfo("pageprobe: dumped %s, %d vmas", tag.c_str(), vmas);
            }
        }
    }).detach();
}

} // namespace pageprobe
#endif

extern "C" {
    void NimMain();
}

int main(int argc, char* argv[])
{
#ifdef Q_OS_ANDROID
    pageprobe::start();
#endif
    Q_INIT_RESOURCE(resources);

    // Must match the qmlcachegen shim style in Status.pro
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE"))
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    qmlRegisterModule("Qt.labs.settings", 1, 1);
    qmlRegisterModule("Qt.labs.settings", 1, 0);

    qmlRegisterModuleImport("Qt.labs.settings", QQmlModuleImportModuleAny,
                       "QtCore", QQmlModuleImportLatest);
#endif

    NimMain();
    return 0;
}
