#include <stdlib.h>
#include <unistd.h>
#include <qqml.h>
#include <QDir>

#ifdef Q_OS_ANDROID
#include <android/log.h>
#include <dirent.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#endif

extern "C" {
    void NimMain();
}

#ifdef Q_OS_ANDROID
// SPIKE diagnostic (drop before upstreaming): with STATUS_SPIKE_MAPS=<dir>, a request file
// <dir>/req-<tag> makes this process copy its own /proc/self/smaps to <dir>/smaps-<tag>
// (release builds are not debuggable, so `run-as` can't read it from outside).
static void spikeSmapsOnRequest(std::string dir)
{
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        DIR* d = opendir(dir.c_str());
        if (!d)
            continue;
        std::vector<std::string> tags;
        while (dirent* e = readdir(d))
            if (!strncmp(e->d_name, "req-", 4))
                tags.emplace_back(e->d_name + 4);
        closedir(d);
        for (const auto& tag : tags) {
            FILE* in = fopen("/proc/self/smaps", "r");
            FILE* out = fopen((dir + "/smaps-" + tag + ".tmp").c_str(), "w");
            char buf[65536];
            size_t n;
            while (in && out && (n = fread(buf, 1, sizeof buf, in)) > 0)
                fwrite(buf, 1, n, out);
            if (in)
                fclose(in);
            if (out)
                fclose(out);
            rename((dir + "/smaps-" + tag + ".tmp").c_str(), (dir + "/smaps-" + tag).c_str());
            unlink((dir + "/req-" + tag).c_str());
            __android_log_print(ANDROID_LOG_INFO, "QMLSPIKE", "smaps dumped %s", tag.c_str());
        }
    }
}
#endif

int main(int argc, char* argv[])
{
#ifdef Q_OS_ANDROID
    if (qEnvironmentVariableIsSet("STATUS_SPIKE_MAPS"))
        std::thread(spikeSmapsOnRequest, qEnvironmentVariable("STATUS_SPIKE_MAPS").toStdString()).detach();
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
