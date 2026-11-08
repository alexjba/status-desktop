#include <StatusReplay/fingerprint.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QJsonArray>
#include <QLibraryInfo>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlIncubationController>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickWindow>
#include <QScreen>

#include <private/qqmlcontextdata_p.h>

#include <typeinfo>

namespace StatusReplay {

namespace {

const char* const EnvironmentKeys[] = {
    "QSG_RENDER_LOOP",
    "QSG_ATLAS_WIDTH",
    "QSG_ATLAS_HEIGHT",
    "QSG_RHI_BACKEND",
    "QT_SCALE_FACTOR",
    "QT_ENABLE_HIGHDPI_SCALING",
    "QT_QUICK_CONTROLS_STYLE",
    "QT_QPA_PLATFORM",
    "QML_DISABLE_DISK_CACHE",
    "QTWEBENGINE_CHROMIUM_FLAGS",
};

QString typeName(const void* object, const std::type_info& type)
{
    return object ? QString::fromLatin1(type.name()) : QString();
}

QString normalizedPath(QString path)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString qtQml = QLibraryInfo::path(QLibraryInfo::QmlImportsPath);
    if (!qtQml.isEmpty() && path.startsWith(qtQml))
        path.replace(0, qtQml.size(), QStringLiteral("$QT_QML"));
    else if (path.startsWith(appDir))
        path.replace(0, appDir.size(), QStringLiteral("$APP"));
    else if (QFileInfo(path).isAbsolute())
        // Checkout-specific dirs (Nim libraries' QML) compare by their tail only.
        path = QStringLiteral("$ABS/") + path.section(QLatin1Char('/'), -2);
    return path;
}

QStringList contextPropertyNames(QQmlEngine* engine)
{
    QStringList names;
    const auto data = QQmlContextData::get(engine->rootContext());
    int misses = 0;
    for (int i = 0; misses < 16 && i < 10000; ++i) {
        const QString name = data->propertyName(i);
        if (name.isEmpty()) {
            ++misses;
            continue;
        }
        misses = 0;
        names.append(name);
    }
    names.sort();
    names.removeDuplicates();
    return names;
}

void flatten(const QJsonValue& value, const QString& prefix, QMap<QString, QString>& out)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            flatten(it.value(), prefix.isEmpty() ? it.key() : prefix + QLatin1Char('.') + it.key(), out);
        return;
    }
    if (value.isArray()) {
        QStringList items;
        for (const auto& item : value.toArray())
            items.append(item.toVariant().toString());
        out.insert(prefix, items.join(QStringLiteral(", ")));
        return;
    }
    out.insert(prefix, value.toVariant().toString());
}

} // namespace

QStringList extraImportPaths(QQmlEngine* engine)
{
    QStringList paths;
    for (const auto& path : engine->importPathList()) {
        if (normalizedPath(path).startsWith(QLatin1String("$ABS/")))
            paths.append(path);
    }
    return paths;
}

QJsonObject setupFingerprint(QQmlEngine* engine)
{
    QJsonObject app { { "name", QCoreApplication::applicationName() },
        { "organization", QCoreApplication::organizationName() },
        { "domain", QCoreApplication::organizationDomain() },
        { "desktopFileName", QGuiApplication::desktopFileName() } };

    QJsonObject env;
    for (const char* key : EnvironmentKeys) {
        if (qEnvironmentVariableIsSet(key))
            env.insert(QString::fromLatin1(key), qEnvironmentVariable(key));
    }

    QJsonArray importPaths;
    for (const auto& path : engine->importPathList())
        importPaths.append(normalizedPath(path));

    const QQmlIncubationController* incubation = engine->incubationController();
    const QQmlNetworkAccessManagerFactory* nam = engine->networkAccessManagerFactory();
    QJsonObject engineJson { { "importPaths", importPaths },
        { "incubationController", incubation ? typeName(incubation, typeid(*incubation)) : QString() },
        { "networkAccessManagerFactory", nam ? typeName(nam, typeid(*nam)) : QString() },
        { "contextProperties", QJsonArray::fromStringList(contextPropertyNames(engine)) },
        { "mainQml", QFile::exists(QStringLiteral(":/main.qml")) } };

    const QScreen* screen = QGuiApplication::primaryScreen();
    QJsonObject rendering { { "graphicsApi", int(QQuickWindow::graphicsApi()) },
        { "sceneGraphBackend", QQuickWindow::sceneGraphBackend() },
        { "devicePixelRatio", screen ? screen->devicePixelRatio() : 0.0 },
        { "font", QGuiApplication::font().family() }, { "fontPointSize", QGuiApplication::font().pointSizeF() } };

    return { { "app", app }, { "env", env }, { "engine", engineJson }, { "rendering", rendering } };
}

QStringList fingerprintDifferences(const QJsonObject& recorded, const QJsonObject& actual)
{
    QMap<QString, QString> expected;
    QMap<QString, QString> got;
    flatten(recorded, {}, expected);
    flatten(actual, {}, got);

    QStringList keys = expected.keys() + got.keys();
    keys.sort();
    keys.removeDuplicates();

    QStringList differences;
    for (const auto& key : std::as_const(keys)) {
        const auto e = expected.constFind(key);
        const auto g = got.constFind(key);
        if (e == expected.cend())
            differences.append(QStringLiteral("%1: not recorded, replay has '%2'").arg(key, *g));
        else if (g == got.cend())
            differences.append(QStringLiteral("%1: recorded '%2', missing in replay").arg(key, *e));
        else if (*e != *g)
            differences.append(QStringLiteral("%1: recorded '%2', replay '%3'").arg(key, *e, *g));
    }
    return differences;
}

} // namespace StatusReplay
