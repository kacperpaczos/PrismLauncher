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

#include "cli/NotesCommands.h"

#include <optional>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTextStream>

#include "Application.h"
#include "BaseInstance.h"
#include "Result.h"
#include "cli/CommandUtils.h"
#include "cli/Output.h"
#include "minecraft/MinecraftInstance.h"

namespace Cli {
namespace {

/**
 * Remove the line break at the end of a text.
 *
 * The line break at the end of a file is a terminator, not a part of the text
 * - keeping it would leave a blank line at the end of the notes.
 */
QString trimTrailingBreak(const QString& text)
{
    if (text.endsWith(QLatin1String("\r\n"))) {
        return text.left(text.size() - 2);
    }
    if (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r'))) {
        return text.left(text.size() - 1);
    }
    return text;
}

/**
 * The text of a `notes set` command.
 *
 * The text is given as an argument, read from a file or read from the
 * standard input.
 */
Result<QString, QString> readNotesText(const Cmd::NotesSet& cmd, bool allowStdin)
{
    if (cmd.file.has_value()) {
        const QFileInfo info(*cmd.file);
        if (!info.exists() || !info.isFile()) {
            return std::unexpected(QStringLiteral("There is no file '%1'").arg(*cmd.file));
        }
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            return std::unexpected(QStringLiteral("The file '%1' could not be read").arg(*cmd.file));
        }
        return trimTrailingBreak(QString::fromUtf8(file.readAll()));
    }
    if (cmd.text.has_value()) {
        return *cmd.text;
    }
    // No text was given - it is read from the standard input.
    // When executed through IPC the read would block the GUI process, so it is refused there.
    if (!allowStdin) {
        return std::unexpected(
            QStringLiteral("Reading the notes from the standard input is not supported while the launcher is running - pass --text or --file"));
    }
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) {
        return std::unexpected(QStringLiteral("The standard input could not be read"));
    }
    return trimTrailingBreak(QString::fromUtf8(input.readAll()));
}

}  // namespace

ExitCode runNotesCommand(NotesContext& ctx, const Cmd::NotesShow& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    // The notes are read through the settings of the instance - the file
    // itself is never touched.
    out.insert(QStringLiteral("notes"), instance->notes());
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runNotesCommand(NotesContext& ctx, const Cmd::NotesSet& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }

    auto text = readNotesText(cmd, ctx.app.isHeadlessRun());
    if (!text) {
        return outputError(QStringLiteral("notes_not_read"), text.error(), ExitCode::NotFound);
    }

    const auto current = instance->notes();
    QString notes;
    if (cmd.append) {
        // The text of the notes is appended to the old one - a line break is
        // added between them, so that the notes stay readable.
        notes = current;
        if (!current.isEmpty() && !current.endsWith(QLatin1Char('\n'))) {
            notes += QLatin1Char('\n');
        }
        notes += text.value();
    } else {
        // The old notes are replaced by the new ones.
        notes = text.value();
    }

    if (notes != current) {
        // The notes are written through the settings of the instance - the
        // file itself is never touched.
        instance->setNotes(notes);
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("notes"), instance->notes());
    out.insert(QStringLiteral("appended"), cmd.append);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace Cli
