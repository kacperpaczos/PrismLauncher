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

#include "cli/LogCommands.h"

#include <algorithm>
#include <array>
#include <deque>
#include <optional>
#include <utility>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include "Application.h"
#include "FileSystem.h"
#include "GZip.h"
#include "MessageLevel.h"
#include "cli/CommandUtils.h"
#include "cli/Output.h"
#include "cli/TaskRunner.h"
#include "logs/AnonymizeLog.h"
#include "logs/LogParser.h"
#include "logs/LogTruncation.h"
#include "minecraft/MinecraftInstance.h"
#include "net/NetJob.h"
#include "net/PasteUpload.h"

namespace Cli {
namespace {

// The log that is read when no file is given.
constexpr auto g_DefaultLogFile = "logs/latest.log";
// The size of the blocks a log file is read in.
constexpr qint64 g_BlockSize = 64 * 1024;
// Upper bound for the upload of a log.
constexpr int g_NetworkTimeoutMs = 180000;
// The number of the lines of a log that are read at most.
constexpr int g_MaxLines = 1000000;

/**
 * The levels of a line of the game log and the names they have in the CLI.
 */
struct LevelName {
    MessageLevel level;
    const char* name;
};

const std::array<LevelName, 8> g_LevelNames{ {
    { MessageLevel::Unknown, "unknown" },
    { MessageLevel::Trace, "trace" },
    { MessageLevel::Debug, "debug" },
    { MessageLevel::Info, "info" },
    { MessageLevel::Message, "message" },
    { MessageLevel::Warning, "warning" },
    { MessageLevel::Warning, "warn" },
    { MessageLevel::Error, "error" },
} };

/**
 * The name of a level of a line of the game log.
 */
QString levelName(MessageLevel level)
{
    switch (level.value()) {
        case MessageLevel::Unknown:
            return QStringLiteral("unknown");
        case MessageLevel::StdOut:
            return QStringLiteral("stdout");
        case MessageLevel::StdErr:
            return QStringLiteral("stderr");
        case MessageLevel::Launcher:
            return QStringLiteral("launcher");
        case MessageLevel::Trace:
            return QStringLiteral("trace");
        case MessageLevel::Debug:
            return QStringLiteral("debug");
        case MessageLevel::Info:
            return QStringLiteral("info");
        case MessageLevel::Message:
            return QStringLiteral("message");
        case MessageLevel::Warning:
            return QStringLiteral("warning");
        case MessageLevel::Error:
            return QStringLiteral("error");
        case MessageLevel::Fatal:
            return QStringLiteral("fatal");
    }
    return QStringLiteral("unknown");
}

/**
 * The level of a name of the `--level` option.
 *
 * The result is empty when the name is not a level of the game log.
 */
std::optional<MessageLevel> parseLevel(const QString& name)
{
    const auto lower = name.toLower();
    for (const auto& entry : g_LevelNames) {
        if (lower == QLatin1String(entry.name)) {
            return entry.level;
        }
    }
    return std::nullopt;
}

/// The names of the levels that the `--level` option accepts.
QString levelNames()
{
    QStringList names;
    for (const auto& entry : g_LevelNames) {
        if (!names.contains(QLatin1String(entry.name))) {
            names.append(QLatin1String(entry.name));
        }
    }
    return names.join(QStringLiteral(", "));
}

/// A line of a log file.
struct LogLine {
    /// The number of the line in the file.
    qint64 number = 0;
    /// The text of the line, without the line break.
    QString text;
};

/// The content of a log file.
struct LogContent {
    QList<LogLine> lines;
    /// true when the file has more lines than the ones that were read.
    bool truncated = false;
};

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
LogContent readPlainHead(QFile& file, int head)
{
    LogContent content;
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
LogContent readPlainTail(QFile& file, int tail)
{
    LogContent content;
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
LogContent readPlainAll(QFile& file)
{
    LogContent content;
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
LogContent readGzLog(QFile& file, int head, int tail)
{
    LogContent content;
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
        qWarning() << "The log could not be decompressed:" << error;
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

/**
 * Read the content of a log file.
 *
 * @param head the number of the lines to read from the beginning (0 = all)
 * @param tail the number of the lines to keep from the end (0 = all)
 */
LogContent readLog(const QString& path, int head, int tail)
{
    LogContent content;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return content;
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

/**
 * The log files of an instance.
 *
 * The files are the ones of the folders that the launcher searches for logs
 * (`crash-reports`, `logs` and the game root itself).
 */
QJsonArray listLogFiles(MinecraftInstance* instance)
{
    const auto basePath = instance->gameRoot();
    QJsonArray files;
    for (const auto& searchPath : instance->getLogFileSearchPaths()) {
        QDir searchDir(searchPath);
        QStringList filters{ "*.log", "*.log.gz" };
        if (searchPath != basePath) {
            filters.append("*.txt");
        }
        const auto entries = searchDir.entryList(filters, QDir::Files | QDir::Readable, QDir::SortFlag::Time);
        for (const auto& name : entries) {
            const QFileInfo info(searchDir.filePath(name));
            QJsonObject entry;
            entry.insert(QStringLiteral("file"), QDir(basePath).relativeFilePath(info.absoluteFilePath()));
            entry.insert(QStringLiteral("size"), static_cast<qint64>(info.size()));
            entry.insert(QStringLiteral("modified"), jsonNullIfEmpty(isoDate(info.lastModified())));
            files.append(entry);
        }
    }
    return files;
}

/**
 * Resolve the file of a log command.
 *
 * The path is relative to the game root of the instance and has to be in one
 * of the folders that are searched for logs.
 */
ExitCode resolveLogFile(MinecraftInstance* instance, const QString& path, QString& absolute)
{
    QFileInfo info(path);
    if (!info.isAbsolute()) {
        info = QFileInfo(FS::PathCombine(instance->gameRoot(), path));
    }
    info = QFileInfo(info.absoluteFilePath());
    if (!info.exists() || !info.isFile()) {
        return outputError(QStringLiteral("log_not_found"), QStringLiteral("There is no log file '%1'").arg(path), ExitCode::NotFound);
    }
    for (const auto& searchPath : instance->getLogFileSearchPaths()) {
        const QString prefix = QDir(searchPath).absolutePath() + QLatin1Char('/');
        if (info.absoluteFilePath().startsWith(prefix)) {
            absolute = info.absoluteFilePath();
            return ExitCode::OK;
        }
    }
    return outputError(QStringLiteral("log_not_found"),
                       QStringLiteral("The file '%1' is not one of the logs of the instance").arg(path),
                       ExitCode::NotFound);
}

/// The name of a paste service.
struct PasteService {
    const char* name;
    PasteUpload::Type type;
};

const std::array<PasteService, 4> g_PasteServices{ {
    { "mclogs", PasteUpload::Type::Mclogs },
    { "paste.gg", PasteUpload::Type::PasteGG },
    { "0x0.st", PasteUpload::Type::NullPointer },
    { "hastebin", PasteUpload::Type::Hastebin },
} };

/// The paste service of a name.
std::optional<PasteUpload::Type> pasteService(const QString& name)
{
    for (const auto& service : g_PasteServices) {
        if (name == QLatin1String(service.name)) {
            return service.type;
        }
    }
    return std::nullopt;
}

/// The names of the paste services that are accepted.
QString pasteServiceNames()
{
    QStringList names;
    for (const auto& service : g_PasteServices) {
        names.append(QLatin1String(service.name));
    }
    return names.join(QStringLiteral(", "));
}

}  // namespace

ExitCode runLogCommand(LogContext& ctx, const Cmd::LogList& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }

    const auto files = listLogFiles(instance);

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("total"), static_cast<int>(files.size()));
    out.insert(QStringLiteral("logs"), files);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runLogCommand(LogContext& ctx, const Cmd::LogRead& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }

    QString path;
    if (auto code = resolveLogFile(instance, cmd.file.value_or(QLatin1String(g_DefaultLogFile)), path); code != ExitCode::OK) {
        return code;
    }

    std::optional<QRegularExpression> pattern;
    if (cmd.grep.has_value()) {
        pattern = QRegularExpression(*cmd.grep);
        if (!pattern->isValid()) {
            return outputError(QStringLiteral("invalid_pattern"),
                               QStringLiteral("'%1' is not a valid regular expression: %2").arg(*cmd.grep, pattern->errorString()),
                               ExitCode::Usage);
        }
    }

    std::optional<MessageLevel> level;
    if (cmd.level.has_value()) {
        level = parseLevel(*cmd.level);
        if (!level.has_value()) {
            return outputError(QStringLiteral("invalid_level"),
                               QStringLiteral("'%1' is not a level of the game log - the levels are: %2").arg(*cmd.level, levelNames()),
                               ExitCode::Usage);
        }
    }

    auto content = readLog(path, cmd.head.value_or(0), cmd.tail.value_or(0));

    // The level of a line is guessed the way the launcher does it - the level
    // of the previous line is used for the lines that continue it.
    QJsonArray lines;
    MessageLevel previous = MessageLevel::Unknown;
    int kept = 0;
    for (const auto& line : content.lines) {
        previous = LogParser::guessLevel(line.text, previous);
        if (pattern.has_value() && !pattern->match(line.text).hasMatch()) {
            continue;
        }
        if (level.has_value() && previous.value() < level->value()) {
            continue;
        }
        if (kept >= g_MaxLines) {
            content.truncated = true;
            break;
        }
        QJsonObject entry;
        entry.insert(QStringLiteral("n"), static_cast<qint64>(line.number));
        entry.insert(QStringLiteral("level"), levelName(previous));
        entry.insert(QStringLiteral("text"), line.text);
        lines.append(entry);
        kept++;
    }

    if (!cmd.json) {
        // Without the JSON output the raw lines are written to the standard
        // output, so that they can be piped into another command.
        QTextStream out(stdout);
        out.setEncoding(QStringConverter::Utf8);
        for (const auto& entry : lines) {
            out << entry.toObject().value(QStringLiteral("text")).toString() << Qt::endl;
        }
        out.flush();
        return ExitCode::OK;
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("file"), QDir(instance->gameRoot()).relativeFilePath(path));
    out.insert(QStringLiteral("total"), static_cast<int>(lines.size()));
    out.insert(QStringLiteral("truncated"), content.truncated);
    out.insert(QStringLiteral("lines"), lines);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runLogCommand(LogContext& ctx, const Cmd::LogUpload& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }

    if (!cmd.assumeYes) {
        // The log is published - it can be read by everybody.
        return outputError(QStringLiteral("confirmation_required"),
                           QStringLiteral("Uploading a log publishes it - use --yes to confirm the upload"),
                           ExitCode::Usage);
    }

    const auto service = pasteService(cmd.service);
    if (!service.has_value()) {
        return outputError(QStringLiteral("invalid_service"),
                           QStringLiteral("'%1' is not a paste service - the services are: %2").arg(cmd.service, pasteServiceNames()),
                           ExitCode::Usage);
    }

    QString path;
    if (auto code = resolveLogFile(instance, cmd.file.value_or(QLatin1String(g_DefaultLogFile)), path); code != ExitCode::OK) {
        return code;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return outputError(QStringLiteral("log_not_found"), QStringLiteral("The log '%1' could not be read").arg(path), ExitCode::Generic);
    }
    QString content = QString::fromUtf8(file.readAll());
    file.close();

    if (cmd.anonymize) {
        // The paste services anonymize the log themselves - the names of the
        // user are replaced either way. The flag is applied here as well, so
        // that it is obvious what it does.
        anonymizeLog(content);
    }

    bool truncated = false;
    if (*service == PasteUpload::Type::Mclogs && content.count(QLatin1Char('\n')) > LogTruncation::g_MaxMclogsLines) {
        content = LogTruncation::truncateLogForMclogs(content);
        truncated = true;
    }

    auto [request, url] = service->make(content, service->defaultBase());
    auto job = makeShared<NetJob>("Log upload", APPLICATION->network());
    job->addNetAction(request);

    if (auto result = runTaskSync(job, g_NetworkTimeoutMs); !result) {
        return outputError(QStringLiteral("upload_failed"), QStringLiteral("The log could not be uploaded: %1").arg(result.error()), ExitCode::Network);
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("file"), QDir(instance->gameRoot()).relativeFilePath(path));
    out.insert(QStringLiteral("service"), cmd.service);
    out.insert(QStringLiteral("truncated"), truncated);
    out.insert(QStringLiteral("url"), jsonNullIfEmpty(*url));
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace Cli
