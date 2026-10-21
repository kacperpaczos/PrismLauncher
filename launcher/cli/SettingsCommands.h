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
// command runners.

#include "cli/Commands.h"
#include "cli/Output.h"

#include <optional>

#include <QString>
#include <QStringList>

namespace Cli {

ExitCode runSettingsCommand(CommandContext& ctx, const Cmd::SettingsList& cmd);
ExitCode runSettingsCommand(CommandContext& ctx, const Cmd::SettingsGet& cmd);
ExitCode runSettingsCommand(CommandContext& ctx, const Cmd::SettingsSet& cmd);
ExitCode runSettingsCommand(CommandContext& ctx, const Cmd::SettingsUnset& cmd);

/**
 * @brief The action a write of the settings performs
 */
enum class SettingsWriteAction {
    /// The value of the setting is changed (`settings set`).
    Set,
    /// The value of the setting is reset to the global one (`settings unset`).
    Unset,
};

/**
 * @brief The error of a write of the settings that was rejected
 */
struct SettingsWriteError {
    /// The machine readable code of the error (the `code` of the envelope).
    QString code;
    /// The human readable message of the error (the `message` of the envelope).
    QString message;
};

/**
 * Checks whether the key may be written through the raw settings commands.
 *
 * The settings that the launcher manages itself (the name of the instance, its
 * uuid, the type of the instance, the play time and the information of a
 * managed pack) are not writable - they have a command of their own.
 *
 * @param key The key of the setting, as it is used by the CLI.
 * @param action The action the write performs.
 * @return The error of the write, or nothing when the key may be written.
 */
std::optional<SettingsWriteError> checkSettingsKeyWritable(const QString& key, SettingsWriteAction action);

/**
 * Checks whether the value may be written for the key.
 *
 * @param key The key of the setting, as it is used by the CLI.
 * @param value The value that is written.
 * @return The error of the write, or nothing when the value is valid.
 */
std::optional<SettingsWriteError> checkSettingsValue(const QString& key, const QString& value);

/**
 * The ids of the settings that the raw settings commands may not write.
 *
 * The settings of a managed pack are not in the list - they all start with
 * `ManagedPack`.
 */
QStringList notWritableSettingsIds();

/**
 * Whether the setting with the given id may be written through the raw
 * settings commands.
 */
bool isWritableSettingsId(const QString& id);

}  // namespace Cli
