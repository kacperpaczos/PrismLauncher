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

namespace Cli {

ExitCode runModCommand(CommandContext& ctx, const Cmd::ModList& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModSearch& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModInfo& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModInstall& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModRemove& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModEnable& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModDisable& cmd);
ExitCode runModCommand(CommandContext& ctx, const Cmd::ModUpdate& cmd);

}  // namespace Cli
