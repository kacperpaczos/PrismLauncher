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

class Application;

namespace Cli {

/// Context handed to every server command runner. It carries the application
/// (instances, settings, network, ...) without pulling Application.h into this
/// header. Construct it once per process/dispatch.
struct ServerContext {
    Application& app;
};

ExitCode runServerCommand(ServerContext& ctx, const Cmd::ServerList& cmd);
ExitCode runServerCommand(ServerContext& ctx, const Cmd::ServerAdd& cmd);
ExitCode runServerCommand(ServerContext& ctx, const Cmd::ServerRemove& cmd);

}  // namespace Cli
