#include <stdlib.h>
#include <unistd.h>
#include <qqml.h>
#include <QDir>

#ifdef STATUS_REPLAY_APP
#include <StatusReplay/replayapp.h>
#else
extern "C" {
    void NimMain();
}
#endif

int main(int argc, char* argv[])
{
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

#ifdef STATUS_REPLAY_APP
    return statusreplay_main(argc, argv);
#else
    NimMain();
    return 0;
#endif
}
