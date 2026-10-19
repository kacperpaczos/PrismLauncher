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

#include <QJsonDocument>
#include <QString>

namespace Cli {

/// Exit codes shared by all headless CLI commands.
enum class ExitCode : int {
    OK = 0,
    Generic = 1,
    Usage = 2,
    NotFound = 3,
    Conflict = 4,
    Ambiguous = 5,
    ParseError = 6,
    Network = 7,
    GuiRequired = 8,
};

/// Print a success envelope `{"ok":true, ...doc}` as compact UTF-8 JSON on stdout.
void outputJson(const QJsonDocument& doc);

/// Print an error envelope `{"ok":false,"error":{"code":...,"message":...}}` on
/// stdout. Returns ExitCode::Generic.
ExitCode outputError(const QString& code, const QString& message);

/// Same as above but returns the supplied exit code.
ExitCode outputError(const QString& code, const QString& message, ExitCode exitCode);

}  // namespace Cli
