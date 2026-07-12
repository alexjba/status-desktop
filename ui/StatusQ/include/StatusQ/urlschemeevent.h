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
            void emitShareTextToQt(const QString& text);
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
            void shareTextActivated(const QString& text);
            void appForegrounded();
    };
}

#endif // STATUSQ_URL_SCHEME_EVENT_H
