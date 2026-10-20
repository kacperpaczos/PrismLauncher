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

// NOTE: this header (and its implementation) must stay free of GUI includes so
// that headless CLI commands can link/use it without a display server.

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <vector>

namespace Cli {

/// Exit codes shared by all headless CLI commands.
enum class ExitCode : int {
    OK = 0,
    Generic = 1,
    Usage = 2,
    NotFound = 3,
    Conflict = 4,
    Ambiguous = 5,
    ParseError = 6,
    Network = 7,
    GuiRequired = 8,
};

/// The sinks the output of the commands is written to.
///
/// By default the output goes to the standard output. The IPC layer installs
/// its own sinks, so that the output of a command that runs in the launcher
/// process can be sent back to the client instead of being printed.
struct OutputSink {
    /// Called with every JSON document (the success or the error envelope).
    std::function<void(const QJsonDocument&)> document;
    /// Called with every piece of raw text (the lines of `log read`).
    std::function<void(const QString&)> text;
};

/// Install the sink the output is written to. An empty sink restores the
/// default behaviour - the standard output.
void setOutputSink(OutputSink sink);

/// Whether a sink is installed.
bool hasOutputSink();

/// Print a success envelope `{"ok":true, ...doc}` as compact UTF-8 JSON on stdout.
void outputJson(const QJsonDocument& doc);

/// Print a document as it is - without the success envelope. It is used to
/// write back the envelope a command produced.
void outputDocument(const QJsonDocument& doc);

/// Print a piece of raw text on stdout. The text is written as it is - the
/// caller adds the line breaks.
void outputText(const QString& text);

/// Print a piece of raw text on stdout, followed by a line break.
void outputLine(const QString& text);

/// Print an error envelope `{"ok":false,"error":{"code":...,"message":...}}` on
/// stdout. Returns ExitCode::Generic.
ExitCode outputError(const QString& code, const QString& message);

/// Same as above but returns the supplied exit code.
ExitCode outputError(const QString& code, const QString& message, ExitCode exitCode);

/// Same as above, but with additional top level fields in the envelope (for
/// example the list of the items that could not be processed).
ExitCode outputError(const QString& code, const QString& message, ExitCode exitCode, const QJsonObject& details);

/// Build an error envelope `{"ok":false,"error":{"code":...,"message":...}}`
/// without writing it to the output.
QJsonObject errorEnvelope(const QString& code, const QString& message);

/**
 * @brief Captures the output of the commands that run in its scope
 *
 * While the capture is alive the output is not written to the standard output
 * - it is collected instead, so that it can be sent back to the client of the
 * IPC.
 */
class OutputCapture {
   public:
    OutputCapture();
    ~OutputCapture();
    OutputCapture(const OutputCapture&) = delete;
    OutputCapture& operator=(const OutputCapture&) = delete;

    /// The documents that were written in the scope of the capture.
    const std::vector<QJsonDocument>& documents() const { return m_documents; }
    /// The raw text that was written in the scope of the capture.
    const QString& text() const { return m_text; }
    /// Whether the command wrote raw text instead of a JSON document.
    bool isRaw() const { return m_documents.empty() && !m_text.isEmpty(); }
    /// The result of the command - the last document it wrote. A command that
    /// only wrote raw text reports it in the `text` field.
    QJsonObject result() const;

   private:
    OutputSink m_previous;
    std::vector<QJsonDocument> m_documents;
    QString m_text;
    /// Set when the captured text hit the size cap - the cap keeps a huge
    /// `log read` from ballooning the IPC response frame.
    bool m_truncated = false;
};

}  // namespace Cli
