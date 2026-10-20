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

#pragma once
#include <QHash>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

class QLocalServer;
class QLocalSocket;
class LockedFile;

class ApplicationId {
   public: /* methods */
    // traditional app = installed system wide and used in a multi-user environment
    static ApplicationId fromTraditionalApp();
    // ID based on a path with all the application data (no two instances with the same data path should run)
    static ApplicationId fromPathAndVersion(const QString& dataPath, const QString& version);
    // custom ID
    static ApplicationId fromCustomId(const QString& id);
    // custom ID, based on a raw string previously acquired from 'toString'
    static ApplicationId fromRawString(const QString& id);

    QString toString() { return m_id; }

   private: /* methods */
    ApplicationId(const QString& value) { m_id = value; }

   private: /* data */
    QString m_id;
};

class LocalPeer : public QObject {
    Q_OBJECT

   public:
    /// The outcome of a request that was sent to the peer.
    enum class ReplyStatus {
        /// The peer answered.
        Ok,
        /// The peer did not answer in time.
        Timeout,
        /// The peer closed the connection without answering - it does not
        /// support the request/response protocol.
        Unsupported,
        /// The request could not be delivered at all.
        Failed,
    };

    /// The answer of the peer.
    struct Reply {
        ReplyStatus status = ReplyStatus::Failed;
        /// The last frame the peer sent.
        QByteArray payload;
    };

    LocalPeer(QObject* parent, const ApplicationId& appId);
    ~LocalPeer();
    bool isClient();
    bool sendMessage(const QByteArray& message, int timeout);

    /**
     * Send a request to the peer and wait for its answer.
     *
     * The frames the peer sends are passed to `onFrame` one by one - the
     * callback returns false when the answer is complete.
     */
    Reply sendRequest(const QByteArray& message, int timeout, const std::function<bool(const QByteArray&)>& onFrame);

    /// Send a frame to the peer that sent the request with the given handle.
    /// The connection is kept open - the peer can send more frames.
    bool sendReply(quint64 handle, const QByteArray& payload, int timeout);

    ApplicationId applicationId() const;

   Q_SIGNALS:
    /// A message was received. The handle identifies the connection it came
    /// from - it is the argument of sendReply() when the message is answered.
    void messageReceived(const QByteArray& message, quint64 handle);

   protected Q_SLOTS:
    void receiveConnection();

   protected:
    ApplicationId id;
    QString socketName;
    std::unique_ptr<QLocalServer> server;
    std::unique_ptr<LockedFile> lockFile;

   private:
    /// Forget the connection of the handle and delete its socket.
    void removeConnection(quint64 handle);

   private:
    quint64 m_nextHandle = 0;
    QHash<quint64, QLocalSocket*> m_connections;
};
