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

#include "cli/Output.h"

#include <QJsonObject>
#include <QTextStream>

#include <QStringConverter>

namespace Cli {
namespace {

// The container/host locale can be plain "C" (ASCII), so the encoding has to be
// set explicitly - otherwise non-ASCII would be mangled or dropped.
void writeDocument(const QJsonDocument& doc)
{
    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);
    out << doc.toJson(QJsonDocument::Compact) << Qt::endl;
    out.flush();
}

}  // namespace

void outputJson(const QJsonDocument& doc)
{
    QJsonObject envelope = doc.object();
    envelope.insert(QStringLiteral("ok"), true);
    writeDocument(QJsonDocument(envelope));
}

ExitCode outputError(const QString& code, const QString& message)
{
    return outputError(code, message, ExitCode::Generic);
}

ExitCode outputError(const QString& code, const QString& message, ExitCode exitCode)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);

    QJsonObject envelope;
    envelope.insert(QStringLiteral("ok"), false);
    envelope.insert(QStringLiteral("error"), error);

    writeDocument(QJsonDocument(envelope));
    return exitCode;
}

}  // namespace Cli
