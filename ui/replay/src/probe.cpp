#include <StatusReplay/probe.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QPointer>
#include <QQmlEngine>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include <QQmlComponent>
#include <QFont>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRawFont>
#include <functional>

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

void dumpSmaps(int index, const QString& directory)
{
#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    QFile smaps(QStringLiteral("/proc/self/smaps"));
    if (!smaps.open(QIODevice::ReadOnly))
        return;
    QFile file(QDir(directory).filePath(QStringLiteral("probe-%1-smaps.tsv").arg(index)));
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QByteArray content = smaps.readAll();
    file.write("mapping\tpss_swappss_kb\n");
    for (const auto& [name, kb] : pssPerMapping(content))
        file.write(QStringLiteral("%1\t%2\n").arg(name).arg(kb).toUtf8());
    // the whole file, to attribute APK-embedded libraries by offset
    if (qEnvironmentVariableIsSet("STATUS_REPLAY_PROBE_RAW_SMAPS")) {
        QFile raw(QDir(directory).filePath(QStringLiteral("probe-%1-smaps.txt").arg(index)));
        if (raw.open(QIODevice::WriteOnly))
            raw.write(content);
    }
#else
    Q_UNUSED(index)
    Q_UNUSED(directory)
#endif
}


// [not for merge] code points of text-like properties that neither the item's font nor Roboto can render
void scanGlyphs(int index, const QString& directory)
{
    QFile out(QDir(directory).filePath(QStringLiteral("probe-%1-glyphs.txt").arg(index)));
    if (!out.open(QIODevice::WriteOnly))
        return;
    const QRawFont roboto = QRawFont::fromFont(QFont(QStringLiteral("Roboto")));
    QSet<QObject*> seen;
    int scanned = 0;
    std::function<void(QObject*)> walk = [&](QObject* o) {
        if (!o || seen.contains(o))
            return;
        seen.insert(o);
        const QMetaObject* mo = o->metaObject();
        for (const char* name : { "text", "displayText", "placeholderText", "preeditText" }) {
            const int pi = mo->indexOfProperty(name);
            if (pi < 0)
                continue;
            const QVariant v = mo->property(pi).read(o);
            if (v.typeId() != QMetaType::QString)
                continue;
            const QString s = v.toString();
            if (s.isEmpty())
                continue;
            ++scanned;
            const int fi = mo->indexOfProperty("font");
            const QFont f = fi >= 0 ? mo->property(fi).read(o).value<QFont>() : QFont();
            const QRawFont raw = QRawFont::fromFont(f);
            QStringList missing;
            for (char32_t c : s.toUcs4()) {
                if (c == U'\n' || c == U' ')
                    continue;
                if (!raw.supportsCharacter(c) && !roboto.supportsCharacter(c))
                    missing << QString::number(uint(c), 16);
            }
            if (missing.isEmpty())
                continue;
            auto* item = qobject_cast<QQuickItem*>(o);
            const QQmlContext* ctx = qmlContext(o);
            QString shown = s.left(60);
            shown.replace(QLatin1Char('\n'), QLatin1String("\\n")).replace(QLatin1Char('\t'), QLatin1String("\\t")).replace(QLatin1Char('\r'), QLatin1String("\\r"));
            out.write(QStringLiteral("%1\t%2\t%3\tvisible=%4\t%5 %6px w%7\tU+%8\t%9\t%10\n")
                          .arg(QString::fromLatin1(mo->className()), o->objectName(), QString::fromLatin1(name))
                          .arg(item ? item->isVisible() : -1)
                          .arg(f.family()).arg(f.pixelSize()).arg(f.weight())
                          .arg(missing.join(QLatin1String(" U+")), shown,
                               ctx ? ctx->baseUrl().toString() : QString())
                          .toUtf8());
        }
        for (QObject* c : o->children())
            walk(c);
        if (auto* item = qobject_cast<QQuickItem*>(o))
            for (QQuickItem* c : item->childItems())
                walk(c);
    };
    for (QWindow* w : QGuiApplication::allWindows())
        if (auto* qw = qobject_cast<QQuickWindow*>(w)) {
            walk(qw);
            walk(qw->contentItem());
        }
    out.write(QStringLiteral("# scanned %1 strings in %2 objects\n").arg(scanned).arg(seen.size()).toUtf8());
}

// [not for merge] first-load time of types loaded by URL: STATUS_REPLAY_PROBE_TYPELOAD="<s>;<url>,<url>..."
void typeLoad(QQmlEngine* engine, const QStringList& urls, const QString& directory)
{
    QFile out(QDir(directory).filePath(QStringLiteral("probe-typeload.txt")));
    if (!out.open(QIODevice::Append))
        return;
    QStringList order = urls;
    if (qEnvironmentVariableIsSet("STATUS_REPLAY_PROBE_TYPELOAD_REVERSE"))
        std::reverse(order.begin(), order.end());
    for (const QString& url : order) {
        QElapsedTimer timer;
        timer.start();
        QQmlComponent component(engine, QUrl(url), QQmlComponent::PreferSynchronous);
        const qint64 ns = timer.nsecsElapsed();
        out.write(QStringLiteral("%1\t%2\t%3\n").arg(ns / 1e6, 0, 'f', 1).arg(url)
                      .arg(component.isError() ? component.errorString().simplified() : QStringLiteral("ok"))
                      .toUtf8());
    }
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
    file.close();
    dumpSmaps(index, directory);
    if (qEnvironmentVariableIsSet("STATUS_REPLAY_PROBE_GLYPHS"))
        scanGlyphs(index, directory);
    qInfo().noquote() << "StatusReplay: probe" << file.fileName() << "loaded" << loaded << "instantiated" << created;
}

} // namespace

QList<QPair<QString, qint64>> pssPerMapping(const QByteArray& smaps)
{
    QHash<QString, qint64> sums;
    QString current;
    for (const QByteArray& line : smaps.split('\n')) {
        if (line.isEmpty())
            continue;
        const int colon = line.indexOf(':');
        const int space = line.indexOf(' ');
        // Mapping headers start with "<from>-<to> perms"; fields with "Name:".
        if (colon < 0 || (space >= 0 && space < colon)) {
            const QList<QByteArray> parts = line.simplified().split(' ');
            if (parts.size() < 6) {
                current = QStringLiteral("[anon]");
            } else {
                const QString path = QString::fromUtf8(parts.mid(5).join(' '));
                current = path.startsWith(QLatin1Char('/')) ? path.section(QLatin1Char('/'), -1) : path;
            }
            continue;
        }
        const QByteArray key = line.left(colon);
        if (key == "Pss" || key == "SwapPss")
            sums[current] += line.mid(colon + 1).simplified().split(' ').value(0).toLongLong();
    }
    QList<QPair<QString, qint64>> result;
    for (auto it = sums.cbegin(); it != sums.cend(); ++it)
        result.append({ it.key(), it.value() });
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    return result;
}

void startProbe(QQmlEngine* engine, const QString& seconds, const QString& directory)
{
    const QStringList times = seconds.split(QLatin1Char(','), Qt::SkipEmptyParts);
    QPointer<QQmlEngine> guarded(engine);
    const QString typeLoadSpec = qEnvironmentVariable("STATUS_REPLAY_PROBE_TYPELOAD");
    if (!typeLoadSpec.isEmpty()) {
        const int at = typeLoadSpec.section(QLatin1Char(';'), 0, 0).toInt();
        const QStringList urls = typeLoadSpec.section(QLatin1Char(';'), 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        QTimer::singleShot(at * 1000, qApp, [guarded, urls, directory] {
            if (guarded)
                typeLoad(guarded, urls, directory);
        });
    }
    for (int i = 0; i < times.size(); ++i) {
        QTimer::singleShot(times[i].toInt() * 1000, qApp, [guarded, i, directory] {
            if (guarded)
                dump(guarded, i + 1, directory);
        });
    }
}

} // namespace StatusReplay
