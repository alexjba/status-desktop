#include "StatusQ/fonts.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QDebug>
#include <QElapsedTimer>
#include <QResource>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRawFont>
#include <QSet>
#include <QTimer>
#include <functional>

using namespace Qt::Literals::StringLiterals;

namespace {
constexpr auto qrcPrefix = ":/assets/fonts/"_L1;

constexpr auto loadFont = [](QLatin1StringView fontRelPath) {
    const auto fontPath = qrcPrefix + fontRelPath;
    const QResource resource(fontPath);
    if (!resource.isValid() || resource.size() == 0) {
        qWarning() << "!!! MISSING OR EMPTY FONT RESOURCE:" << fontPath;
        return -1;
    }
    if (resource.compressionAlgorithm() != QResource::NoCompression) {
        qWarning() << "!!! FONT RESOURCE MUST BE UNCOMPRESSED:" << fontPath;
        return -1;
    }

    // fonts.qrc is compiled into StatusQ and outlives the font database. Keep a
    // non-owning view to avoid Qt's resource readAll() copy; platform font
    // backends may still make their own copies
    // (currently all platforms respect that, except Window GDI backend)
    const auto data = QByteArray::fromRawData(
        reinterpret_cast<const char *>(resource.data()), resource.size());
    const auto id = QFontDatabase::addApplicationFontFromData(data);
    if (id == -1)
        qWarning() << "!!! COULDN'T LOAD FONT FROM:" << fontPath;
    return id;
};

constexpr auto loadAndAssignFont = [](QLatin1StringView fontRelPath, QFont& font) {
    const auto id = loadFont(fontRelPath);
    if (id != -1) {
        const auto families = QFontDatabase::applicationFontFamilies(id);
        if (families.isEmpty()) {
            qWarning() << "!!! EMPTY FONT ???" << fontRelPath;
            return -1;
        }
        font = QFont(families);
    }
    return id;
};
}

namespace {
// DIAG (spike, remove): every 2 s for 120 s, log texts with characters their own font lacks
void diagTextScan(int round)
{
    static QSet<QString> seen;
    std::function<void(QQuickItem*)> scan = [&](QQuickItem* item) {
        const auto text = item->property("text");
        const auto font = item->property("font");
        if (text.isValid() && font.isValid()) {
            const auto str = text.toString();
            const auto f = font.value<QFont>();
            const auto raw = QRawFont::fromFont(f);
            for (const auto ucs4: str.toUcs4()) {
                if (ucs4 > 0x20 && !raw.supportsCharacter(ucs4)) {
                    QStringList chain;
                    for (QObject* o = item; o && chain.size() < 6; o = o->parent())
                        chain << QString::fromLatin1(o->metaObject()->className()) + QLatin1Char(':') + o->objectName();
                    const auto key = QString::number(ucs4, 16) + str.left(40) + chain.join(QString());
                    if (!seen.contains(key)) {
                        seen.insert(key);
                        qWarning().noquote() << "[diag-text]" << round << QString::number(ucs4, 16) << f.family()
                                             << f.pixelSize() << f.weight() << item->isVisible()
                                             << str.left(60).toUtf8().toHex() << chain.join(QLatin1String(" < "));
                    }
                    break;
                }
            }
        }
        for (auto child: item->childItems())
            scan(child);
    };
    for (auto window: QGuiApplication::topLevelWindows())
        if (auto quickWindow = qobject_cast<QQuickWindow*>(window))
            scan(quickWindow->contentItem());
    if (round < 60)
        QTimer::singleShot(2000, [round] { diagTextScan(round + 1); });
}
}

Fonts::Fonts(QObject *parent)
    : QObject(parent)
{
    initFonts();
    if (qEnvironmentVariableIntValue("STATUS_DIAG_TEXT_SCAN") == 1)
        QTimer::singleShot(1000, [] { diagTextScan(0); });
}

void Fonts::initFonts()
{
#ifdef QT_DEBUG
    QElapsedTimer t;
    t.start();
#endif
    // Inter (baseFont & monoFont) styles
    const auto baseFontId = loadAndAssignFont("InterVariable.ttf"_L1, m_baseFont);
    loadFont("InterVariable-Italic.ttf"_L1);

    // Roboto Mono (codeFont)
    loadAndAssignFont("RobotoMono-VariableFont_wght.ttf"_L1, m_codeFont);
    loadFont("RobotoMono-Italic-VariableFont_wght.ttf"_L1);

    // Emoji are rendered by a COLRv0 font generated from the Twemoji artwork
    // (see tools/emoji). Registering it as the application emoji font makes Qt
    // resolve emoji clusters to it instead of whatever the system happens to
    // ship, so all platforms render the same glyphs.
    const auto emojiFontId = loadFont("StatusTwemoji-COLRv0.ttf"_L1);
    if (emojiFontId != -1) {
        const auto emojiFamilies = QFontDatabase::applicationFontFamilies(emojiFontId);
        if (emojiFamilies.isEmpty()) {
            qWarning() << "!!! EMPTY EMOJI FONT ???";
        } else {
            QFontDatabase::setApplicationEmojiFontFamilies(emojiFamilies);

            // The artwork is taller than the em (it spans the text line box, see
            // tools/emoji), so a caller sizing an emoji to a box needs the ratio.
            // Measure it once rather than hardcoding what the font happens to be.
            m_emojiFont = QFont(emojiFamilies);

            constexpr auto referencePixelSize = 256;
            QFont referenceFont = m_emojiFont;
            referenceFont.setPixelSize(referencePixelSize);
            const auto ink = QFontMetricsF(referenceFont)
                                 .tightBoundingRect(QStringLiteral("\U0001F600"));
            if (ink.height() > 0)
                m_emojiArtRatio = ink.height() / referencePixelSize;
        }
    }

    // ▴/▾ (24h price change) are not in Inter. Without an app fallback Qt opens every system
    // family in turn (about 100 on Android, each kept for the process lifetime) looking for them.
    const auto symbolsFontId = loadFont("StatusSymbols.ttf"_L1);
    if (symbolsFontId != -1) {
        for (const auto& family: QFontDatabase::applicationFontFamilies(symbolsFontId))
            QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Common, family);
    }

    // monoFont fine tuning (https://rsms.me/inter/#features)
    if (baseFontId != -1) {
        m_monoFont = m_baseFont;
        for (auto tag: {"tnum", "ss02"}) {
            const auto feature = QFont::Tag::fromString(tag);
            if (feature)
                m_monoFont.setFeature(*feature, true);
        }
    }

#ifdef QT_DEBUG
    qInfo()
        << "!!! ADDING FONTS TOOK" << t.nsecsElapsed() / 1'000'000.f << "ms";
#endif
}
