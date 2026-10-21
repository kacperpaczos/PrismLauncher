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

#include "logs/LogReader.h"

#include <algorithm>
#include <deque>

#include <QByteArray>
#include <QFile>
#include <QStringList>

#include "GZip.h"

namespace LogReader {
namespace {

// The size of the blocks a log file is read in.
constexpr qint64 g_BlockSize = 64 * 1024;

/// Remove the line break of a line of a log.
QString cleanLine(const QByteArray& line)
{
    auto text = QString::fromUtf8(line);
    if (text.endsWith(QLatin1Char('\r'))) {
        text.chop(1);
    }
    return text;
}

/**
 * Split the content of a log into its lines.
 *
 * A file that does not end with a newline still has a last line.
 */
QStringList splitLines(const QByteArray& content)
{
    QStringList lines;
    qsizetype start = 0;
    for (qsizetype i = 0; i < content.size(); ++i) {
        if (content.at(i) != '\n') {
            continue;
        }
        lines.append(cleanLine(content.mid(start, i - start)));
        start = i + 1;
    }
    if (start < content.size()) {
        // The file does not end with a newline - the rest is the last line.
        lines.append(cleanLine(content.mid(start)));
    }
    return lines;
}

/// The number of the line breaks in a part of a file.
qint64 countNewlines(QFile& file, qint64 from, qint64 to)
{
    qint64 count = 0;
    file.seek(from);
    qint64 remaining = to - from;
    while (remaining > 0) {
        const QByteArray block = file.read(qMin(g_BlockSize, remaining));
        if (block.isEmpty()) {
            break;
        }
        count += block.count('\n');
        remaining -= block.size();
    }
    return count;
}

/**
 * Read the first lines of a plain log file.
 *
 * The file is read in blocks, so that a big log is never loaded into memory
 * as a whole.
 */
Content readPlainHead(QFile& file, int head)
{
    Content content;
    QByteArray pending;
    qint64 number = 0;

    while (!file.atEnd()) {
        const QByteArray block = file.read(g_BlockSize);
        if (block.isEmpty()) {
            break;
        }
        QByteArray data = pending + block;
        pending.clear();
        qsizetype start = 0;
        for (qsizetype i = 0; i < data.size(); ++i) {
            if (data.at(i) != '\n') {
                continue;
            }
            number++;
            if (content.lines.size() >= head) {
                content.truncated = true;
                return content;
            }
            content.lines.append({ number, cleanLine(data.mid(start, i - start)) });
            start = i + 1;
        }
        pending = data.mid(start);
    }

    if (!pending.isEmpty()) {
        // The file does not end with a newline - the rest is the last line.
        number++;
        if (content.lines.size() >= head) {
            content.truncated = true;
        } else {
            content.lines.append({ number, cleanLine(pending) });
        }
    }
    return content;
}

/**
 * Read the last lines of a plain log file.
 *
 * The file is read from its end in blocks, so that a big log is never loaded
 * into memory as a whole.
 */
Content readPlainTail(QFile& file, int tail)
{
    Content content;
    const qint64 size = file.size();

    // The end of the file is read backwards until there are more line breaks
    // than the number of the lines that are wanted.
    QByteArray region;
    qint64 position = size;
    qint64 breaks = 0;
    while (position > 0 && breaks <= tail) {
        const qint64 chunk = qMin(g_BlockSize, position);
        position -= chunk;
        file.seek(position);
        const QByteArray block = file.read(chunk);
        region.prepend(block);
        breaks += block.count('\n');
    }

    // The number of the lines that are in front of the region.
    qint64 before = countNewlines(file, 0, position);
    if (position > 0) {
        // The region starts in the middle of a line - that line is not part of
        // the result.
        before++;
        const auto firstBreak = region.indexOf('\n');
        if (firstBreak >= 0) {
            region = region.mid(firstBreak + 1);
        } else {
            region.clear();
        }
    }

    const auto lines = splitLines(region);
    const int count = lines.size();
    const int first = qMax(0, count - tail);
    for (int i = first; i < count; ++i) {
        // The first line of the region is the line `before + 1` of the file.
        content.lines.append({ before + i + 1, lines.at(i) });
    }
    content.truncated = first > 0;
    return content;
}

/// Read all the lines of a plain log file.
Content readPlainAll(QFile& file)
{
    Content content;
    QByteArray pending;
    qint64 number = 0;

    while (!file.atEnd()) {
        const QByteArray block = file.read(g_BlockSize);
        if (block.isEmpty()) {
            break;
        }
        QByteArray data = pending + block;
        pending.clear();
        qsizetype start = 0;
        for (qsizetype i = 0; i < data.size(); ++i) {
            if (data.at(i) != '\n') {
                continue;
            }
            number++;
            content.lines.append({ number, cleanLine(data.mid(start, i - start)) });
            start = i + 1;
        }
        pending = data.mid(start);
    }
    if (!pending.isEmpty()) {
        number++;
        content.lines.append({ number, cleanLine(pending) });
    }
    return content;
}

/**
 * Read the lines of a gzipped log file.
 *
 * A gzipped file cannot be seeked in, so it is streamed through
 * `GZip::readGzFileByBlocks`. Only the lines that are wanted are kept.
 */
Result<Content, QString> readGzLog(QFile& file, int head, int tail)
{
    Content content;
    QByteArray pending;
    std::deque<QString> lastLines;
    qint64 number = 0;
    bool more = false;

    const auto error = GZip::readGzFileByBlocks(&file, [&content, &pending, &lastLines, &number, &more, head, tail](const QByteArray& block) {
        // The block is a view into a buffer that is reused - it has to be
        // copied before it is used.
        QByteArray data = pending + block;
        pending.clear();
        qsizetype start = 0;
        for (qsizetype i = 0; i < data.size(); ++i) {
            if (data.at(i) != '\n') {
                continue;
            }
            number++;
            if (head > 0 && content.lines.size() >= head) {
                more = true;
                return false;
            }
            const auto line = cleanLine(data.mid(start, i - start));
            if (tail > 0) {
                lastLines.push_back(line);
                while (lastLines.size() > static_cast<size_t>(tail)) {
                    lastLines.pop_front();
                }
            } else {
                content.lines.append({ number, line });
            }
            start = i + 1;
        }
        pending = data.mid(start);
        return true;
    });

    if (!error.isEmpty()) {
        // The file could not be decompressed - the content would be wrong,
        // so the reading fails.
        return std::unexpected(QStringLiteral("the log could not be decompressed: %1").arg(error));
    }

    if (!pending.isEmpty()) {
        // The file does not end with a newline - the rest is the last line.
        number++;
        if (tail > 0) {
            lastLines.push_back(cleanLine(pending));
            while (lastLines.size() > static_cast<size_t>(tail)) {
                lastLines.pop_front();
            }
        } else {
            content.lines.append({ number, cleanLine(pending) });
        }
    }

    if (tail > 0) {
        const qint64 first = number - static_cast<qint64>(lastLines.size()) + 1;
        qint64 lineNumber = first;
        for (const auto& line : lastLines) {
            content.lines.append({ lineNumber++, line });
        }
        content.truncated = first > 1;
        return content;
    }

    content.truncated = more;
    return content;
}

}  // namespace

Result<Content, QString> read(const QString& path, int head, int tail)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(QStringLiteral("the log could not be opened: %1").arg(file.errorString()));
    }
    if (path.endsWith(QLatin1String(".gz"))) {
        return readGzLog(file, head, tail);
    }
    if (tail > 0) {
        return readPlainTail(file, tail);
    }
    if (head > 0) {
        return readPlainHead(file, head);
    }
    return readPlainAll(file);
}

}  // namespace LogReader
