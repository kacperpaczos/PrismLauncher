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
/// from ballooning the IPC response frame. The cap counts the bytes of the
/// UTF-8 encoding of the text.
constexpr qsizetype g_MaxCapturedTextBytes = 1024 * 1024;

/// The size of the UTF-8 encoding of a character.
qsizetype utf8Size(QChar ch)
{
    const auto code = ch.unicode();
    if (code < 0x80) {
        return 1;
    }
    if (code < 0x800) {
        return 2;
    }
    return 3;
}

/**
 * The text, cut so that the UTF-8 encoding of it is at most `room` bytes
 * long.
 *
 * The cut is at a character boundary, so that the text stays valid UTF-8.
 */
QString cutToBytes(const QString& text, qsizetype room)
{
    if (text.toUtf8().size() <= room) {
        return text;
    }

    qsizetype used = 0;
    qsizetype count = 0;
    for (const auto ch : text) {
        const auto size = utf8Size(ch);
        if (used + size > room) {
            break;
        }
        used += size;
        ++count;
    }
    return text.left(count);
}

}  // namespace

OutputCapture::OutputCapture() : m_previous(g_sink)
{
    setOutputSink(OutputSink{
        .document = [this](const QJsonDocument& doc) { m_documents.push_back(doc); },
        .text = [this](const QString& text) {
            m_wroteText = true;
            if (m_textBytes >= g_MaxCapturedTextBytes) {
                // The cap is reached - the rest of the text is dropped.
                m_truncated = true;
                return;
            }
            const auto room = g_MaxCapturedTextBytes - m_textBytes;
            const auto bytes = text.toUtf8().size();
            if (bytes <= room) {
                m_text += text;
                m_textBytes += bytes;
                return;
            }
            // The cap counts the bytes of the UTF-8 encoding - the text is
            // cut at the cap.
            m_truncated = true;
            const auto cut = cutToBytes(text, room);
            m_text += cut;
            m_textBytes += cut.toUtf8().size();
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
    if (m_wroteText) {
        // A raw write - the text is reported even when it is empty.
        result.insert(QStringLiteral("text"), m_text);
        result.insert(QStringLiteral("truncated"), m_truncated);
    }
    return result;
}

}  // namespace Cli
