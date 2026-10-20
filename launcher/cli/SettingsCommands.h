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

namespace Cli {

ExitCode runSettingsCommand(ModContext& ctx, const Cmd::SettingsList& cmd);
ExitCode runSettingsCommand(ModContext& ctx, const Cmd::SettingsGet& cmd);
ExitCode runSettingsCommand(ModContext& ctx, const Cmd::SettingsSet& cmd);
ExitCode runSettingsCommand(ModContext& ctx, const Cmd::SettingsUnset& cmd);

}  // namespace Cli
