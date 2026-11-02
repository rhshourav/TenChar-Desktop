#pragma once
// HTTPS transport on Qt Network. Qt uses the operating system's TLS stack and certificate store, which is why
// the core's OpenSSL is not used for this: OpenSSL alone would not see the Windows or macOS root certificates.
// One QNetworkAccessManager lives on its own thread; send() blocks the calling worker thread until it is done.
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QThread>

#include "tc/relay.hpp"

class QtTransportWorker;

class QtTransport final : public tc::HttpTransport {
public:
    /// `extraCaPem` is a test seam (a self-signed server). The app never passes it.
    explicit QtTransport(QString userAgent, QByteArray extraCaPem = {});
    ~QtTransport() override;
    tc::HttpResponse send(const tc::HttpRequest& request) override;

private:
    QThread thread_;
    QtTransportWorker* worker_ = nullptr;
};
