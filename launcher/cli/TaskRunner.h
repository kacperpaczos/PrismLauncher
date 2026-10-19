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

// NOTE: no GUI includes - this is used by headless CLI commands.

#include <QString>

#include "Result.h"
#include "tasks/Task.h"

namespace Cli {

/// Run `task` to completion on a local event loop, blocking the calling thread.
///
/// A single throttled progress line is written to stderr while the task runs.
/// On timeout the task is aborted (when possible) and an error is returned.
///
/// @returns an empty result on success, or the failure reason / timeout message.
Result<void, QString> runTaskSync(Task::Ptr task, int timeoutMs = 300000);

}  // namespace Cli
