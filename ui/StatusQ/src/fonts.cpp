#include "StatusQ/fonts.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QDebug>
#include <QElapsedTimer>
#include <QResource>
#include <QTimer>
#ifdef Q_OS_ANDROID
#include <android/log.h>
#include <dlfcn.h>
#include <unwind.h>
#endif

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

#ifdef Q_OS_ANDROID
namespace {
// DIAG (spike, remove): backtrace of every font match whose message contains STATUS_DIAG_FONT_BT
QtMessageHandler previousHandler = nullptr;
QByteArray diagNeedle;

_Unwind_Reason_Code unwindFrame(_Unwind_Context* context, void* arg)
{
    auto frames = static_cast<QList<uintptr_t>*>(arg);
    if (const auto pc = _Unwind_GetIP(context))
        frames->append(pc);
    return frames->size() < 60 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

void diagHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if (context.category && qstrcmp(context.category, "qt.text.font.match") == 0
        && msg.toUtf8().contains(diagNeedle)) {
        QList<uintptr_t> frames;
        _Unwind_Backtrace(unwindFrame, &frames);
        __android_log_print(ANDROID_LOG_WARN, "FONTBT", "match: %s", qPrintable(msg.left(200)));
        for (const auto pc: frames) {
            Dl_info info{};
            if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname)
                __android_log_print(ANDROID_LOG_WARN, "FONTBT", "  %s+0x%lx %s+0x%lx", info.dli_fname,
                                    (unsigned long)(pc - uintptr_t(info.dli_fbase)),
                                    info.dli_sname ? info.dli_sname : "?",
                                    (unsigned long)(info.dli_saddr ? pc - uintptr_t(info.dli_saddr) : 0));
        }
    }
    if (previousHandler)
        previousHandler(type, context, msg);
}
}
#endif

Fonts::Fonts(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_ANDROID
    diagNeedle = qgetenv("STATUS_DIAG_FONT_BT");
    if (!diagNeedle.isEmpty()) {
        // the Nim side installs its own handler later; stay in front of whatever is current
        auto keepFirst = new QTimer(this);
        keepFirst->setInterval(200);
        connect(keepFirst, &QTimer::timeout, this, [] {
            const auto current = qInstallMessageHandler(diagHandler);
            if (current != diagHandler) {
                previousHandler = current;
                __android_log_print(ANDROID_LOG_WARN, "FONTBT", "handler (re)installed");
            }
        });
        keepFirst->start();
        previousHandler = qInstallMessageHandler(diagHandler);
    }
#endif
    initFonts();
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
