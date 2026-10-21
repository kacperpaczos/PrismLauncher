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

// NOTE: this header must stay free of GUI includes - the reading of a log is
// used by the headless `log read` command.

#include <QList>
#include <QString>

#include "Result.h"

namespace LogReader {

/// A line of a log file.
struct Line {
    /// The number of the line in the file.
    qint64 number = 0;
    /// The text of the line, without the line break.
    QString text;
};

/// The content of a log file.
struct Content {
    /// The lines that were read.
    QList<Line> lines;
    /// true when the file has more lines than the ones that were read.
    bool truncated = false;
};

/**
 * @brief Read the content of a log file
 *
 * A big log is never loaded into memory as a whole: the tail of a plain file
 * is read from its end in blocks and a gzipped file is streamed through
 * `GZip::readGzFileByBlocks`, keeping only the lines that are wanted.
 *
 * A file that does not end with a newline still has a last line.
 *
 * A file that cannot be opened and a file that cannot be decompressed are
 * failures - they are not an empty log.
 *
 * @param path  the file to read
 * @param head  the number of the lines to read from the beginning (0 = all)
 * @param tail  the number of the lines to keep from the end (0 = all)
 *
 * @returns the content of the file, or the reason of the failure.
 */
Result<Content, QString> read(const QString& path, int head, int tail);

}  // namespace LogReader
