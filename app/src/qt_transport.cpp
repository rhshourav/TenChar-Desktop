#include "qt_transport.hpp"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QUrl>
#include <future>
#include <memory>

namespace {
constexpr int TransferTimeoutMs = 45'000;      // no data for this long: give up (free relays can take a while to wake)
constexpr qint64 MaxResponseBytes = 256 * 1024;
}  // namespace

class QtTransportWorker : public QObject {
    Q_OBJECT
public:
    QtTransportWorker(QString ua, QByteArray ca) : userAgent_(std::move(ua)), extraCa_(std::move(ca)) {}

    void start(tc::HttpRequest req, std::shared_ptr<std::promise<tc::HttpResponse>> done) {
        if (!nam_) nam_ = new QNetworkAccessManager(this);
        QUrl url(QString::fromStdString(req.url));
        if (!url.isValid() || url.scheme() != QLatin1String("https")) { done->set_value({}); return; }
        QNetworkRequest r(url);
        r.setRawHeader("Authorization", QByteArray("Bearer ") + QByteArray::fromStdString(req.bearer));
        r.setRawHeader("Accept", "application/json");
        r.setRawHeader("User-Agent", userAgent_.toUtf8());
        r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);  // never follow
        r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        r.setTransferTimeout(TransferTimeoutMs);
        if (!extraCa_.isEmpty()) {
            QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
            cfg.addCaCertificates(QSslCertificate::fromData(extraCa_));
            r.setSslConfiguration(cfg);
        }
        QNetworkReply* reply = nullptr;
        const QByteArray body = QByteArray::fromStdString(req.body);
        if (req.method == "GET") reply = nam_->get(r);
        else if (req.method == "DELETE") reply = nam_->deleteResource(r);
        else if (req.method == "POST") { r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"); reply = nam_->post(r, body); }
        else { done->set_value({}); return; }

        auto buffer = std::make_shared<QByteArray>();
        auto tooBig = std::make_shared<bool>(false);
        connect(reply, &QIODevice::readyRead, reply, [reply, buffer, tooBig] {
            buffer->append(reply->readAll());
            if (buffer->size() > MaxResponseBytes) { *tooBig = true; reply->abort(); }
        });
        connect(reply, &QNetworkReply::finished, reply, [reply, buffer, tooBig, done] {
            buffer->append(reply->readAll());
            tc::HttpResponse out;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (!*tooBig && status > 0 && buffer->size() <= MaxResponseBytes) {
                out.transportOk = true;
                out.status = status;
                out.body = buffer->toStdString();
            }
            reply->deleteLater();
            done->set_value(std::move(out));
        });
    }

private:
    QString userAgent_;
    QByteArray extraCa_;
    QNetworkAccessManager* nam_ = nullptr;
};

QtTransport::QtTransport(QString userAgent, QByteArray extraCaPem) {
    worker_ = new QtTransportWorker(std::move(userAgent), std::move(extraCaPem));
    worker_->moveToThread(&thread_);
    thread_.start();
}

QtTransport::~QtTransport() {
    thread_.quit();
    thread_.wait();
    delete worker_;
}

tc::HttpResponse QtTransport::send(const tc::HttpRequest& request) {
    auto promise = std::make_shared<std::promise<tc::HttpResponse>>();
    auto future = promise->get_future();
    QMetaObject::invokeMethod(worker_, [w = worker_, request, promise] { w->start(request, promise); }, Qt::QueuedConnection);
    return future.get();
}

#include "qt_transport.moc"
