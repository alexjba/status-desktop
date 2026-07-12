#ifndef STATUSQ_URL_SCHEME_EVENT_H
#define STATUSQ_URL_SCHEME_EVENT_H

#include <QObject>
#include <QUrl>

namespace Status
{
    class UrlSchemeEvent : public QObject
    {
        Q_OBJECT

        public:
            void emitDeepLinkToQt(const QString& url);
            void emitShareToQt(const QString& text, const QStringList& imagePaths);
            void emitAppForegroundedToQt();
            static void setInstance(UrlSchemeEvent* instance);

            void registerUrlHandler();
            void watchApplicationState();

        protected:
            bool eventFilter(QObject* obj, QEvent* event) override;

        public slots:
            void handleUrl(const QUrl& url);

        signals:
            void urlActivated(const QString& url);
            // Share-target hand-off: shared text/links plus app-private cached
            // copies of shared images, as a JSON array of absolute paths (a
            // JSON string keeps the Nim slot signature to plain QStrings).
            void shareActivated(const QString& text, const QString& imagePathsJson);
            void appForegrounded();
    };
}

#endif // STATUSQ_URL_SCHEME_EVENT_H
