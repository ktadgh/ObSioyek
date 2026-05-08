#include <OpenWithApplication.h>

bool OpenWithApplication::event(QEvent* event) {
    if (event->type() == QEvent::FileOpen) {
        QFileOpenEvent* openEvent = static_cast<QFileOpenEvent*>(event);
        QUrl url = openEvent->url();
        if (url.isValid() && url.scheme() == "sioyek")
            emit file_ready(url.toString());
        else
            emit file_ready(openEvent->file());
    }

    return QApplication::event(event);
}
