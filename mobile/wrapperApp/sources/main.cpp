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
#include <chrono>
#include <fcntl.h>
#include <thread>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTimer>

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

// [not for merge] probe: STATUS_FAULT=<flag>[,<flag>...], applied at main() entry.
// random: MADV_RANDOM on our read-only lib segments (no fault-around/read-around from here on)
// drop: MADV_DONTNEED them too, so residency afterwards ~= pages touched after main
// evict: POSIX_FADV_DONTNEED the unmapped page cache of base.apk (cold-cache proxy)
// again: re-apply random 8 s later (libs dlopen'ed after main)
// Legacy: random = random,drop; random_keep = random; dontneed0 = drop.
QElapsedTimer probeClock;

void adviseLibraries(bool random, bool drop, const char *what)
{
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
    qInfo("fault-probe: %s on %zu ranges, %zu MB, %d failed", what, ranges.size(), bytes >> 20, failures);
}

void evictApkPageCache()
{
    char path[512] = {};
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[1024];
    while (maps && fgets(line, sizeof line, maps)) {
        if (const char *p = strstr(line, "/data/app/")) {
            if (const char *e = strstr(p, "base.apk")) {
                snprintf(path, sizeof path, "%.*s", int(e - p + 8), p);
                break;
            }
        }
    }
    if (maps)
        fclose(maps);
    const int fd = path[0] ? open(path, O_RDONLY) : -1;
    const int rc = fd >= 0 ? posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) : -1;
    if (fd >= 0)
        close(fd);
    qInfo("fault-probe: evict %s rc %d", path, rc);
}

void applyFaultProbe()
{
    probeClock.start();
    const char *env = getenv("STATUS_FAULT");
    if (!env)
        return;
    const QByteArray mode(env);
    const bool legacyRandom = mode == "random";
    const bool random = legacyRandom || mode == "random_keep" || mode.split(',').contains("random");
    const bool drop = legacyRandom || mode == "dontneed0" || mode.split(',').contains("drop");
    if (mode.split(',').contains("evict"))
        evictApkPageCache();
    if (random || drop)
        adviseLibraries(random, drop, env);
    if (random && mode.split(',').contains("again")) {
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::seconds(8));
            adviseLibraries(true, false, "again");
        }).detach();
    }
    // first frame after main(), to compare start-up latency between modes
    std::thread([] {
        while (!QCoreApplication::instance())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        QMetaObject::invokeMethod(QCoreApplication::instance(), [] {
            auto *poll = new QTimer(QCoreApplication::instance());
            QObject::connect(poll, &QTimer::timeout, poll, [poll] {
                for (QWindow *w : QGuiApplication::topLevelWindows()) {
                    if (auto *qw = qobject_cast<QQuickWindow *>(w)) {
                        poll->stop();
                        QObject::connect(qw, &QQuickWindow::frameSwapped, qw, [] {
                            static int frames = 0;
                            if (++frames == 1 || frames == 30)
                                qInfo("fault-probe: frame %d at %lld ms after main", frames,
                                      static_cast<long long>(probeClock.elapsed()));
                        });
                        return;
                    }
                }
            });
            poll->start(10);
        }, Qt::QueuedConnection);
    }).detach();
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
