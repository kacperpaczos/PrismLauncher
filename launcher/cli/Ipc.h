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

#pragma once

// NOTE: this header (and its implementation) must stay free of GUI includes so
// that headless CLI commands can link/use it without a display server.

#include <QJsonObject>
#include <QString>

#include "cli/Commands.h"

class LocalPeer;

namespace Cli {

/// The outcome of a command that was sent to the already running launcher.
struct IpcResult {
    enum class Status {
        /// The launcher answered.
        Ok,
        /// The launcher did not answer in time.
        Timeout,
        /// The running launcher is too old to answer - it does not know the
        /// request/response protocol of the IPC.
        Unsupported,
        /// The command could not be sent at all.
        Failed,
    };

    /// How the command ended.
    Status status = Status::Failed;
    /// Whether the command succeeded.
    bool ok = false;
    /// The exit code of the command.
    int exitCode = 0;
    /// The JSON envelope the command produced.
    QJsonObject data;
    /// Whether the output of the command was raw text instead of a JSON
    /// envelope.
    bool rawOutput = false;
    /// The code of the error (empty when the command succeeded).
    QString errorCode;
    /// The message of the error.
    QString errorMessage;
};

/**
 * @brief Send the command to the already running launcher and wait for its
 * answer
 *
 * The launcher executes the command in its own process and answers with the
 * result of the command - the JSON envelope it produced and its exit code.
 * The timeout is the number of milliseconds the client waits for the answer.
 */
IpcResult sendCommandToPeer(LocalPeer& peer, const Cli::Command& cmd, int timeoutMs);

}  // namespace Cli
