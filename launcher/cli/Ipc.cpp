// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "cli/Ipc.h"

#include <QUuid>

#include "ApplicationMessage.h"
#include "LocalPeer.h"

namespace Cli {

IpcResult sendCommandToPeer(LocalPeer& peer, const Cli::Command& cmd, int timeoutMs)
{
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto request = ApplicationMessage::request(cmd, requestId);

    IpcResult result;
    bool answered = false;

    const auto reply = peer.sendRequest(request.serialize(), timeoutMs, [&result, &answered](const QByteArray& payload) {
        ApplicationMessage message;
        const auto parsed = message.parse(payload);
        if (!parsed) {
            // A broken answer - there is no point in waiting for another one.
            return false;
        }

        if (message.type == ApplicationMessageType::Accepted) {
            // The launcher understood the request and will answer it.
            return true;
        }

        if (message.type != ApplicationMessageType::Response) {
            return true;
        }

        result.ok = message.ok;
        result.exitCode = message.exitCode;
        result.data = message.data;
        result.rawOutput = message.rawOutput;
        result.errorCode = message.errorCode;
        result.errorMessage = message.errorMessage;
        answered = true;
        return false;
    });

    switch (reply.status) {
        case LocalPeer::ReplyStatus::Ok:
            result.status = answered ? IpcResult::Status::Ok : IpcResult::Status::Failed;
            break;
        case LocalPeer::ReplyStatus::Timeout:
            result.status = IpcResult::Status::Timeout;
            break;
        case LocalPeer::ReplyStatus::Unsupported:
            result.status = IpcResult::Status::Unsupported;
            break;
        case LocalPeer::ReplyStatus::Failed:
            result.status = IpcResult::Status::Failed;
            break;
    }

    return result;
}

}  // namespace Cli
