/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt Solutions component.
**
** $QT_BEGIN_LICENSE:BSD$
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of Digia Plc and its Subsidiary(-ies) nor the names
**     of its contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "LocalPeer.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QTime>
#include "LockedFile.h"

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <QLibrary>
typedef BOOL(WINAPI* PProcessIdToSessionId)(DWORD, DWORD*);
static PProcessIdToSessionId pProcessIdToSessionId = 0;
#endif
#if defined(Q_OS_UNIX)
#include <sys/types.h>
#include <unistd.h>
#endif

#include <QCryptographicHash>
#include <chrono>
#include <thread>

static const char* ack = "ack";

namespace {

/// The outcome of a read from the socket.
enum class ReadResult {
    Ok,
    /// The peer closed the connection.
    Closed,
    /// The peer did not send anything in time.
    Timeout,
};

/// Read exactly `size` bytes from the socket.
ReadResult readExactly(QLocalSocket& socket, char* buffer, qint64 size, int timeout)
{
    qint64 got = 0;
    while (got < size) {
        if (socket.bytesAvailable() == 0) {
            // The data that is already in the buffer does not make
            // waitForReadyRead return - it waits for *new* data. So the
            // buffer has to be checked first.
            if (!socket.waitForReadyRead(timeout)) {
                if (socket.state() != QLocalSocket::ConnectedState) {
                    // The peer closed the connection.
                    return ReadResult::Closed;
                }
                return ReadResult::Timeout;
            }
        }
        const qint64 read = socket.read(buffer + got, size - got);
        if (read <= 0) {
            // The peer closed the connection.
            return ReadResult::Closed;
        }
        got += read;
    }
    return ReadResult::Ok;
}

/// Read a frame (a length prefixed payload) from the socket.
ReadResult readFrame(QLocalSocket& socket, QByteArray& payload, int timeout)
{
    char lengthBytes[4];
    const auto lengthResult = readExactly(socket, lengthBytes, 4, timeout);
    if (lengthResult != ReadResult::Ok) {
        return lengthResult;
    }

    // The length is written by QDataStream, which uses the big endian byte
    // order by default.
    const quint32 length = (static_cast<quint32>(static_cast<quint8>(lengthBytes[0])) << 24) |
                           (static_cast<quint32>(static_cast<quint8>(lengthBytes[1])) << 16) |
                           (static_cast<quint32>(static_cast<quint8>(lengthBytes[2])) << 8) |
                           static_cast<quint32>(static_cast<quint8>(lengthBytes[3]));

    payload.resize(static_cast<qsizetype>(length));
    if (length == 0) {
        return ReadResult::Ok;
    }
    return readExactly(socket, payload.data(), length, timeout);
}

/// The number of milliseconds that are left until the deadline.
int remainingMs(const std::chrono::steady_clock::time_point& deadline)
{
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (left <= 0) {
        return 0;
    }
    return static_cast<int>(left);
}

}  // namespace

ApplicationId ApplicationId::fromTraditionalApp()
{
    QString protoId = QCoreApplication::applicationFilePath();
#if defined(Q_OS_WIN)
    protoId = protoId.toLower();
#endif
    auto prefix = protoId.section(QLatin1Char('/'), -1);
    static const QRegularExpression s_removeChars("[^a-zA-Z]");
    prefix.remove(s_removeChars);
    prefix.truncate(6);
    QByteArray idc = protoId.toUtf8();
    quint16 idNum = qChecksum(idc);
    auto socketName = QLatin1String("pl") + prefix + QLatin1Char('-') + QString::number(idNum, 16).left(12);
#if defined(Q_OS_WIN)
    if (!pProcessIdToSessionId) {
        QLibrary lib("kernel32");
        pProcessIdToSessionId = (PProcessIdToSessionId)lib.resolve("ProcessIdToSessionId");
    }
    if (pProcessIdToSessionId) {
        DWORD sessionId = 0;
        pProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
        socketName += QLatin1Char('-') + QString::number(sessionId, 16);
    }
#else
    socketName += QLatin1Char('-') + QString::number(::getuid(), 16);
#endif
    return ApplicationId(socketName);
}

ApplicationId ApplicationId::fromPathAndVersion(const QString& dataPath, const QString& version)
{
    QCryptographicHash shasum(QCryptographicHash::Algorithm::Sha1);
    QString result = dataPath + QLatin1Char('-') + version;
    shasum.addData(result.toUtf8());
    return ApplicationId(QLatin1String("pl") + QString::fromLatin1(shasum.result().toHex()).left(12));
}

ApplicationId ApplicationId::fromCustomId(const QString& id)
{
    return ApplicationId(QLatin1String("pl") + id);
}

ApplicationId ApplicationId::fromRawString(const QString& id)
{
    return ApplicationId(id);
}

LocalPeer::LocalPeer(QObject* parent, const ApplicationId& appId) : QObject(parent), id(appId)
{
    socketName = id.toString();
    server.reset(new QLocalServer());
    QString lockName = QDir(QDir::tempPath()).absolutePath() + QLatin1Char('/') + socketName + QLatin1String("-lockfile");
    lockFile.reset(new LockedFile(lockName));
    lockFile->open(QIODevice::ReadWrite);
}

LocalPeer::~LocalPeer() {}

ApplicationId LocalPeer::applicationId() const
{
    return id;
}

bool LocalPeer::isClient()
{
    if (lockFile->isLocked())
        return false;

    if (!lockFile->lock(LockedFile::WriteLock, false))
        return true;

    bool res = server->listen(socketName);
#if defined(Q_OS_UNIX)
    // ### Workaround
    if (!res && server->serverError() == QAbstractSocket::AddressInUseError) {
        QLocalServer::removeServer(socketName);
        res = server->listen(socketName);
    }
#endif
    if (!res)
        qWarning("QtSingleCoreApplication: listen on local socket failed, %s", qPrintable(server->errorString()));
    connect(server.get(), &QLocalServer::newConnection, this, &LocalPeer::receiveConnection);
    return false;
}

bool LocalPeer::sendMessage(const QByteArray& message, int timeout)
{
    if (!isClient())
        return false;

    QLocalSocket socket;
    bool connOk = false;
    int tries = 2;
    for (int i = 0; i < tries; i++) {
        // Try twice, in case the other instance is just starting up
        socket.connectToServer(socketName);
        connOk = socket.waitForConnected(timeout / 2);
        if (!connOk && i < (tries - 1)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    if (!connOk) {
        return false;
    }

    QByteArray uMsg(message);
    QDataStream ds(&socket);

    ds.writeBytes(uMsg.constData(), uMsg.size());
    if (!socket.waitForBytesWritten(timeout)) {
        return false;
    }

    // wait for 'ack'
    if (!socket.waitForReadyRead(timeout)) {
        return false;
    }

    // make sure we got 'ack'
    if (!(socket.read(qstrlen(ack)) == ack)) {
        return false;
    }
    return true;
}

LocalPeer::Reply LocalPeer::sendRequest(const QByteArray& message, int timeout, const std::function<bool(const QByteArray&)>& onFrame)
{
    if (!isClient()) {
        return { ReplyStatus::Failed, {} };
    }

    QLocalSocket socket;
    bool connOk = false;
    int tries = 2;
    for (int i = 0; i < tries; i++) {
        // Try twice, in case the other instance is just starting up
        socket.connectToServer(socketName);
        connOk = socket.waitForConnected(timeout / 2);
        if (!connOk && i < (tries - 1)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    if (!connOk) {
        return { ReplyStatus::Failed, {} };
    }

    QDataStream ds(&socket);
    ds.writeBytes(message.constData(), message.size());
    socket.flush();
    if (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(timeout)) {
        return { ReplyStatus::Failed, {} };
    }

    // wait for 'ack'
    char ackBytes[3];
    const auto ackResult = readExactly(socket, ackBytes, 3, timeout);
    if (ackResult != ReadResult::Ok || qstrncmp(ackBytes, ack, 3) != 0) {
        return { ReplyStatus::Failed, {} };
    }

    // The peer answers with one or more frames. The first one acknowledges the
    // request, the last one is the answer itself.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    while (true) {
        const int left = remainingMs(deadline);
        if (left <= 0) {
            return { ReplyStatus::Timeout, {} };
        }

        QByteArray payload;
        const auto result = readFrame(socket, payload, left);
        if (result == ReadResult::Closed) {
            // The peer closed the connection without answering - it does not
            // know the request/response protocol.
            return { ReplyStatus::Unsupported, {} };
        }
        if (result == ReadResult::Timeout) {
            return { ReplyStatus::Timeout, {} };
        }

        if (!onFrame(payload)) {
            return { ReplyStatus::Ok, payload };
        }
    }
}

bool LocalPeer::sendReply(quint64 handle, const QByteArray& payload, int timeout)
{
    auto socket = m_connections.value(handle, nullptr);
    if (!socket) {
        return false;
    }

    QDataStream ds(socket);
    ds.writeBytes(payload.constData(), payload.size());
    socket->flush();
    if (socket->bytesToWrite() > 0 && !socket->waitForBytesWritten(timeout)) {
        return false;
    }
    return true;
}

void LocalPeer::removeConnection(quint64 handle)
{
    auto socket = m_connections.take(handle);
    if (socket) {
        socket->deleteLater();
    }
}

void LocalPeer::receiveConnection()
{
    QLocalSocket* socket = server->nextPendingConnection();
    if (!socket) {
        return;
    }

    while (socket->bytesAvailable() < static_cast<int>(sizeof(quint32))) {
        socket->waitForReadyRead();
    }
    QDataStream ds(socket);
    QByteArray uMsg;
    quint32 remaining;
    ds >> remaining;
    uMsg.resize(remaining);
    int got = 0;
    char* uMsgBuf = uMsg.data();
    do {
        got = ds.readRawData(uMsgBuf, remaining);
        remaining -= got;
        uMsgBuf += got;
    } while (remaining && got >= 0 && socket->waitForReadyRead(2000));
    if (got < 0) {
        qWarning("QtLocalPeer: Message reception failed %s", socket->errorString().toLatin1().constData());
        delete socket;
        return;
    }
    socket->write(ack, qstrlen(ack));
    socket->waitForBytesWritten(1000);

    // The connection is kept open, so that the peer can answer a request on
    // it. It is closed when the peer disconnects.
    const quint64 handle = ++m_nextHandle;
    m_connections.insert(handle, socket);
    connect(socket, &QLocalSocket::disconnected, this, [this, handle]() { removeConnection(handle); });

    emit messageReceived(uMsg, handle);  // ### (might take a long time to return)
}
