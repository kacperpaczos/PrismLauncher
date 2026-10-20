#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>

#include "Result.h"

#include "cli/Commands.h"

/// The kind of an IPC message.
enum class ApplicationMessageType {
    /// A command to execute. The messages without an explicit type are
    /// requests too - that is how the first version of the IPC worked.
    Request,
    /// The peer understood the request and will answer it.
    Accepted,
    /// The answer of the peer.
    Response,
};

/// A message of the IPC between the launcher and the command line client.
///
/// The first version of the IPC was fire and forget: the client sent a command
/// and did not learn its result. The messages that carry a request id are the
/// second version of the protocol - the peer answers them with a response
/// message that carries the same request id.
struct ApplicationMessage {
    /// The kind of the message.
    ApplicationMessageType type = ApplicationMessageType::Request;
    /// The id of the request. It is empty in the messages of the first
    /// version of the protocol - those are not answered.
    QString requestId;
    /// The command of a request.
    QString command;
    /// The arguments of a request.
    QHash<QString, QString> args;

    /// Whether the command of a response succeeded.
    bool ok = false;
    /// The exit code of the command of a response.
    int exitCode = 0;
    /// The output of the command of a response - the JSON envelope it
    /// produced.
    QJsonObject data;
    /// Whether the output of the command was raw text instead of a JSON
    /// envelope.
    bool rawOutput = false;
    /// The code of the error of a response (empty when the command succeeded).
    QString errorCode;
    /// The message of the error of a response.
    QString errorMessage;

    /// Whether the message is a request of a command.
    bool isRequest() const { return type == ApplicationMessageType::Request; }
    /// Whether the message is the answer of a request.
    bool isResponse() const { return type == ApplicationMessageType::Response; }
    /// Whether the message expects an answer of the peer.
    bool expectsReply() const { return isRequest() && !requestId.isEmpty(); }

    QByteArray serialize() const;
    Result<> parse(const QByteArray& input);

    static ApplicationMessage fromCliCommand(const Cli::Command& cmd);
    /// A request that is answered by the peer.
    static ApplicationMessage request(const Cli::Command& cmd, const QString& requestId);
    /// The acknowledgement of a request - the peer understood it and will
    /// answer it.
    static ApplicationMessage accepted(const QString& requestId);
    /// The answer of a request. `data` is the JSON envelope the command
    /// produced.
    static ApplicationMessage response(const QString& requestId, bool ok, int exitCode, const QJsonObject& data);

    Result<Cli::Command> toCliCommand() const;
};
