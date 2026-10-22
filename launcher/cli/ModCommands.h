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

// NOTE: keep this header free of GUI includes - it is used by the headless
// dispatch in Application and main.

#include "cli/Commands.h"
#include "cli/Output.h"

#include <QDir>
#include <QJsonArray>
#include <QList>
#include <QString>

namespace Cli {

/// A mod that was disabled before an update and has to stay disabled after
/// it - the download writes the file with its plain name, so it has to be
/// renamed back to `.disabled` once it is on the disk.
struct DisabledModRename {
    /// The name of the mod (it goes into the report).
    QString name;
    /// The file of the mod before the update (the `.disabled` file).
    QString oldFileName;
    /// The file the download writes (the plain name).
    QString newFileName;
};

/**
 * @brief restore the disabled state of the updated mods
 *
 * The decision is made per pair, on what is actually on the disk: the
 * freshly downloaded file that exists is renamed to `.disabled`, the file
 * that does not exist is skipped - its download failed, so the old
 * `.disabled` file of the mod is still in place and the mod never left its
 * disabled state.
 *
 * @param dir     the directory of the mods
 * @param renames the mods that have to stay disabled
 * @returns the entries of the `failed` array for the renames that could not
 *          be restored (every one of them carries the code `io`) - empty
 *          when everything was restored or skipped
 */
QJsonArray restoreDisabledMods(const QDir& dir, const QList<DisabledModRename>& renames);

ExitCode runModCommand(CommandContext& ctx, const Cmd::ModList& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModSearch& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModInfo& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModInstall& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModRemove& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModEnable& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModDisable& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModUpdate& cmd);

}  // namespace Cli
