#include <StatusReplay/replayapp.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QQmlApplicationEngine>
#include <QResource>

extern "C" {
void statusq_registerQmlTypes();
void statusq_setupNetworkAccessManagerFactory(void* engine, const char* tmpPath, qint64 maxCacheSize);
void statusq_initializeWebEngine();
void statusq_installBoostedIncubationController(void* engine, int msPerTick, int gentlePeriodMs, int boostGapMs);
void statusq_installBackgroundGraphicsRelease();
}

namespace StatusReplay {

namespace {

// Values from src/constants.nim and src/nim_status_client.nim.
constexpr qint64 NetworkDiskCacheSize = 512LL * 1024 * 1024;
constexpr int IncubationMsPerTick = 12;
constexpr int IncubationGentlePeriodMs = 300;
constexpr int IncubationBoostGapMs = 2;

QString resourcesPath()
{
    const QString overridden = qEnvironmentVariable("STATUS_REPLAY_RCC");
    if (!overridden.isEmpty())
        return overridden;
#if defined(Q_OS_WIN)
    return QCoreApplication::applicationDirPath() + QStringLiteral("/../resources/resources.rcc");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/../resources.rcc");
#endif
}

void setDefaultEnv(const char* name, const QByteArray& value)
{
    if (!qEnvironmentVariableIsSet(name))
        qputenv(name, value);
}

} // namespace

void prepareProcess(const QJsonObject& manifest)
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    setDefaultEnv("QSG_ATLAS_WIDTH", "1024");
    setDefaultEnv("QSG_ATLAS_HEIGHT", "1024");
#endif

    statusq_initializeWebEngine();

    // Nim derives the scale from its 'ui-scale' file; replay applies the recorded result.
    const QString scale =
        manifest["fingerprint"].toObject()["env"].toObject()["QT_SCALE_FACTOR"].toString();
    if (!scale.isEmpty())
        setDefaultEnv("QT_SCALE_FACTOR", scale.toUtf8());

    setDefaultEnv("QSG_RENDER_LOOP", "threaded");

    QCoreApplication::setApplicationName(QStringLiteral("Status Desktop"));
    QCoreApplication::setOrganizationName(QStringLiteral("Status"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("status.app"));
    QCoreApplication::setApplicationVersion(manifest["app"].toObject()["version"].toString());
    QGuiApplication::setDesktopFileName(QStringLiteral("nim-status"));
}

void prepareApplication(const QJsonObject&)
{
    const QString rcc = resourcesPath();
    if (!QResource::registerResource(rcc))
        qWarning() << "StatusReplay: cannot register" << rcc;
}

void prepareEngine(QQmlApplicationEngine* engine, const QJsonObject& manifest, const QString& tempDirectory)
{
    engine->addImportPath(QStringLiteral("qrc:/"));
    engine->addImportPath(QStringLiteral("qrc:/./imports"));
    engine->addImportPath(QStringLiteral("qrc:/./app"));

    // QML modules shipped by Nim libraries (NimQtCamera, NimQrScan) add absolute source dirs.
    // addImportPath prepends, so add them last-first to keep the recorded order.
    const QStringList present = engine->importPathList();
    const QJsonArray recorded = manifest["importPaths"].toArray();
    for (qsizetype i = recorded.size() - 1; i >= 0; --i) {
        const QString path = recorded.at(i).toString();
        if (QFileInfo(path).isAbsolute() && QFileInfo::exists(path) && !present.contains(path))
            engine->addImportPath(path);
    }

    QDir().mkpath(tempDirectory);
    statusq_setupNetworkAccessManagerFactory(
        engine, QDir(tempDirectory).filePath(QStringLiteral("netcache")).toUtf8().constData(), NetworkDiskCacheSize);
    statusq_installBoostedIncubationController(
        engine, IncubationMsPerTick, IncubationGentlePeriodMs, IncubationBoostGapMs);
#if defined(Q_OS_ANDROID)
    statusq_installBackgroundGraphicsRelease();
#endif
    statusq_registerQmlTypes();
}

} // namespace StatusReplay
