#include <StatusReplay/probe.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QQmlEngine>
#include <QSet>
#include <QTimer>

#include <private/qqmlmetatype_p.h>
#include <private/qqmltype_p.h>
#include <private/qqmltypeloader_p.h>
#include <private/qv4engine_p.h>
#include <private/qv4executablecompilationunit_p.h>

#if defined(Q_OS_DARWIN)
#include <malloc/malloc.h>
#elif defined(__GLIBC__) || defined(Q_OS_ANDROID)
#include <malloc.h>
#endif

namespace StatusReplay {

namespace {

double mallocInUseMB()
{
#if defined(Q_OS_DARWIN)
    malloc_statistics_t stats;
    malloc_zone_statistics(nullptr, &stats);
    return stats.size_in_use / 1048576.0;
#elif defined(__GLIBC__) || defined(Q_OS_ANDROID)
    const struct mallinfo info = mallinfo();
    return info.uordblks / 1048576.0;
#else
    return 0;
#endif
}

void dump(QQmlEngine* engine, int index, const QString& directory)
{
    auto v4 = engine->handle();
    QSet<QString> instantiated;
    for (const auto& unit : v4->compilationUnits())
        instantiated.insert(unit->finalUrlString());

    QString rows;
    int loaded = 0;
    int created = 0;
    QSet<QString> seen;
    for (const QQmlType& type : QQmlMetaType::qmlAllTypes()) {
        if (!type.isValid() || !(type.isComposite() || type.isCompositeSingleton()) || type.isInlineComponentType())
            continue;
        const QUrl url = type.sourceUrl();
        const QString name = url.toString();
        if (seen.contains(name))
            continue;
        seen.insert(name);
        const bool isLoaded = v4->typeLoader()->isTypeLoaded(url);
        if (!isLoaded && !QQmlMetaType::obtainCompilationUnit(url))
            continue;
        ++loaded;
        const bool isCreated = instantiated.contains(name);
        created += isCreated;
        rows += QStringLiteral("%1\t%2\n").arg(name).arg(isCreated);
    }

    QDir().mkpath(directory);
    QFile file(QDir(directory).filePath(QStringLiteral("probe-%1.tsv").arg(index)));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QStringLiteral("#t=%1 loaded=%2 instantiated=%3 mallocMB=%4\nurl\tinstantiated\n")
                   .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                   .arg(loaded)
                   .arg(created)
                   .arg(mallocInUseMB(), 0, 'f', 2)
                   .toUtf8());
    file.write(rows.toUtf8());
    qInfo().noquote() << "StatusReplay: probe" << file.fileName() << "loaded" << loaded << "instantiated" << created;
}

} // namespace

void startProbe(QQmlEngine* engine, const QString& seconds, const QString& directory)
{
    const QStringList times = seconds.split(QLatin1Char(','), Qt::SkipEmptyParts);
    QPointer<QQmlEngine> guarded(engine);
    for (int i = 0; i < times.size(); ++i) {
        QTimer::singleShot(times[i].toInt() * 1000, qApp, [guarded, i, directory] {
            if (guarded)
                dump(guarded, i + 1, directory);
        });
    }
}

} // namespace StatusReplay
