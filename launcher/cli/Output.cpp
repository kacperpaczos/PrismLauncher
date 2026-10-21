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

#include "cli/Output.h"

#include <QJsonObject>
#include <QTextStream>

#include <QStringConverter>

namespace Cli {
namespace {

// The sink the output is written to. It is empty by default - the output goes
// to the standard output then.
OutputSink g_sink;

// The container/host locale can be plain "C" (ASCII), so the encoding has to be
// set explicitly - otherwise non-ASCII would be mangled or dropped.
void writeDocument(const QJsonDocument& doc)
{
    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);
    out << doc.toJson(QJsonDocument::Compact) << Qt::endl;
    out.flush();
}

void writeText(const QString& text)
{
    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);
    out << text;
    out.flush();
}

}  // namespace

void setOutputSink(OutputSink sink)
{
    g_sink = std::move(sink);
}

bool hasOutputSink()
{
    return static_cast<bool>(g_sink.document) || static_cast<bool>(g_sink.text);
}

void outputJson(const QJsonDocument& doc)
{
    if (!doc.isObject()) {
        // A non-object document cannot be merged into the success envelope;
        // refusing is safer than silently emitting `{"ok":true}`.
        outputError(QStringLiteral("internal_error"), QStringLiteral("outputJson expects a JSON object document"));
        return;
    }

    QJsonObject envelope = doc.object();
    envelope.insert(QStringLiteral("ok"), true);
    outputDocument(QJsonDocument(envelope));
}

void outputDocument(const QJsonDocument& doc)
{
    if (g_sink.document) {
        g_sink.document(doc);
        return;
    }

    writeDocument(doc);
}

void outputText(const QString& text)
{
    if (g_sink.text) {
        g_sink.text(text);
        return;
    }

    writeText(text);
}

void outputLine(const QString& text)
{
    // The line break is a part of the text, so that the captured output is
    // exactly what the standard output would have got.
    outputText(text + QLatin1Char('\n'));
}

ExitCode outputError(const QString& code, const QString& message)
{
    return outputError(code, message, ExitCode::Generic);
}

ExitCode outputError(const QString& code, const QString& message, ExitCode exitCode)
{
    return outputError(code, message, exitCode, QJsonObject());
}

ExitCode outputError(const QString& code, const QString& message, ExitCode exitCode, const QJsonObject& details)
{
    QJsonObject envelope = errorEnvelope(code, message);
    for (auto it = details.constBegin(); it != details.constEnd(); ++it) {
        // The status of the envelope must not be overwritten by the details.
        if (it.key() == QLatin1String("ok") || it.key() == QLatin1String("error")) {
            continue;
        }
        envelope.insert(it.key(), it.value());
    }

    outputDocument(QJsonDocument(envelope));
    return exitCode;
}

QJsonObject errorEnvelope(const QString& code, const QString& message)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);

    QJsonObject envelope;
    envelope.insert(QStringLiteral("ok"), false);
    envelope.insert(QStringLiteral("error"), error);
    return envelope;
}

ExitCode reportError(const CliError& error)
{
    return outputError(error.error, error.message, error.code, error.details);
}

namespace {

/// Upper bound for the text captured during one command - keeps `log read`
/// from ballooning the IPC response frame.
constexpr qsizetype g_MaxCapturedTextBytes = 1024 * 1024;

}  // namespace

OutputCapture::OutputCapture() : m_previous(g_sink)
{
    setOutputSink(OutputSink{
        .document = [this](const QJsonDocument& doc) { m_documents.push_back(doc); },
        .text = [this](const QString& text) {
            if (m_text.size() >= g_MaxCapturedTextBytes) {
                m_truncated = true;
                return;
            }
            m_text += text.left(g_MaxCapturedTextBytes - m_text.size());
            if (m_text.size() >= g_MaxCapturedTextBytes) {
                m_truncated = true;
            }
        },
    });
}

OutputCapture::~OutputCapture()
{
    setOutputSink(m_previous);
}

QJsonObject OutputCapture::result() const
{
    if (!m_documents.empty()) {
        return m_documents.back().object();
    }

    QJsonObject result;
    if (!m_text.isEmpty()) {
        result.insert(QStringLiteral("text"), m_text);
    }
    if (m_truncated) {
        result.insert(QStringLiteral("truncated"), true);
    }
    return result;
}

}  // namespace Cli
