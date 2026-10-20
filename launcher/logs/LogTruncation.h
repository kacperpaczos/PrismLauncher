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

// NOTE: this header must stay free of GUI includes - the truncation of a log
// is used by the headless `log upload` command as well.

#include <QString>

namespace LogTruncation {

/// The number of the lines that mclo.gs accepts.
constexpr int g_MaxMclogsLines = 25000;
/// The number of the lines that are kept at the beginning of a log.
constexpr int g_InitialMclogsLines = 10000;
/// The number of the lines that are kept at the end of a log.
constexpr int g_FinalMclogsLines = 14900;

/**
 * @brief Shorten a log so that it fits into the limit of mclo.gs
 *
 * The first and the last lines of the log are kept and the middle of it is
 * replaced with a notice. A log that is short enough is not changed.
 */
QString truncateLogForMclogs(const QString& logContent);

}  // namespace LogTruncation
