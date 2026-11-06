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

namespace {

// Our libraries are mapped from the APK: without this, each page fault maps the whole 64 KB
// fault-around window of code/read-only data, most of which is never used.
int adviseRandomAccess(dl_phdr_info *info, size_t, void *)
{
    if (!info->dlpi_name || !strstr(info->dlpi_name, "/data/app/"))
        return 0;
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
        const uintptr_t begin = (info->dlpi_addr + ph.p_vaddr + pageSize - 1) & ~(pageSize - 1);
        const uintptr_t end = (info->dlpi_addr + ph.p_vaddr + ph.p_filesz) & ~(pageSize - 1);
        if (end <= begin || (relroEnd > begin && relroBegin < end))
            continue;
        madvise(reinterpret_cast<void *>(begin), end - begin, MADV_RANDOM);
    }
    return 0;
}

} // namespace
#endif

int main(int argc, char* argv[])
{
#ifdef Q_OS_ANDROID
    dl_iterate_phdr(adviseRandomAccess, nullptr);
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
