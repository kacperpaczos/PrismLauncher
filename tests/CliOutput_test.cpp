// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTemporaryFile>

#include <cstdio>
#include <functional>

#include "cli/Output.h"

#ifdef Q_OS_WIN
#include <io.h>
#define close _close
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace {

/// Redirect the C stdout to a temp file for the duration of `fn` and return
/// everything written to it. Needed because the output helpers write straight to
/// stdout (as they must for a CLI).
QByteArray captureStdout(const std::function<void()>& fn)
{
    QTemporaryFile tmp;
    if (!tmp.open()) {
        return {};
    }
    const QString path = tmp.fileName();
    tmp.close();

    std::fflush(stdout);
    const int saved = dup(fileno(stdout));
    if (saved < 0) {
        return {};
    }

    if (std::freopen(QFile::encodeName(path).constData(), "w", stdout) == nullptr) {
        dup2(saved, fileno(stdout));
        close(saved);
        return {};
    }

    fn();
    std::fflush(stdout);

    dup2(saved, fileno(stdout));
    close(saved);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QJsonObject parseObject(const QByteArray& raw, bool* ok = nullptr)
{
    const auto doc = QJsonDocument::fromJson(raw);
    if (ok != nullptr) {
        *ok = !doc.isNull() && doc.isObject();
    }
    return doc.object();
}

}  // namespace

class CliOutputTest : public QObject {
    Q_OBJECT

   private slots:
    void test_outputJson_wrapsOkEnvelope()
    {
        const QByteArray raw = captureStdout([] {
            QJsonObject data;
            data.insert("value", 42);
            Cli::outputJson(QJsonDocument(data));
        });

        bool ok = false;
        const QJsonObject root = parseObject(raw, &ok);
        QVERIFY(ok);
        QCOMPARE(root.value("ok").toBool(), true);
        QCOMPARE(root.value("value").toInt(), 42);
    }

    void test_outputJson_rejectsNonObject()
    {
        const QByteArray raw = captureStdout([] { Cli::outputJson(QJsonDocument(QJsonArray{ 1, 2, 3 })); });

        bool ok = false;
        const QJsonObject root = parseObject(raw, &ok);
        QVERIFY(ok);
        QCOMPARE(root.value("ok").toBool(), false);
        QCOMPARE(root.value("error").toObject().value("code").toString(), QString("internal_error"));
    }

    void test_outputError_defaultExitCode()
    {
        Cli::ExitCode result = Cli::ExitCode::OK;
        const QByteArray raw = captureStdout([&result] {
            result = Cli::outputError("not_found", "no such instance");
        });

        QCOMPARE(result, Cli::ExitCode::Generic);

        bool ok = false;
        const QJsonObject root = parseObject(raw, &ok);
        QVERIFY(ok);
        QCOMPARE(root.value("ok").toBool(), false);
        const QJsonObject error = root.value("error").toObject();
        QCOMPARE(error.value("code").toString(), QString("not_found"));
        QCOMPARE(error.value("message").toString(), QString("no such instance"));
    }

    void test_outputError_explicitExitCode()
    {
        Cli::ExitCode result = Cli::ExitCode::OK;
        captureStdout([&result] {
            result = Cli::outputError("usage", "bad flags", Cli::ExitCode::Usage);
        });

        QCOMPARE(result, Cli::ExitCode::Usage);
        QCOMPARE(static_cast<int>(Cli::ExitCode::Usage), 2);
        QCOMPARE(static_cast<int>(Cli::ExitCode::GuiRequired), 8);
    }

    void test_outputError_utf8()
    {
        const QString message = QString::fromUtf8("żółć: brak instancji");
        const QByteArray raw = captureStdout([&message] {
            Cli::outputError("not_found", message, Cli::ExitCode::NotFound);
        });

        bool ok = false;
        const QJsonObject root = parseObject(raw, &ok);
        QVERIFY(ok);
        QCOMPARE(root.value("error").toObject().value("message").toString(), message);
    }

    void test_reportError_writesTheEnvelopeOfTheError()
    {
        Cli::ExitCode result = Cli::ExitCode::OK;
        const QByteArray raw = captureStdout([&result] {
            QJsonObject details;
            details.insert("failed", QJsonArray{ QJsonObject{ { "target", "nope" } } });
            result = Cli::reportError(Cli::CliError{ .code = Cli::ExitCode::Ambiguous,
                                                   .error = "ambiguous_target",
                                                   .message = "Some targets matched more than one mod",
                                                   .details = details });
        });

        // The exit code of the error is kept - the orchestrator of a command
        // writes the error exactly once.
        QCOMPARE(result, Cli::ExitCode::Ambiguous);
        QCOMPARE(static_cast<int>(Cli::ExitCode::Ambiguous), 5);

        bool ok = false;
        const QJsonObject root = parseObject(raw, &ok);
        QVERIFY(ok);
        QCOMPARE(root.value("ok").toBool(), false);
        const QJsonObject error = root.value("error").toObject();
        QCOMPARE(error.value("code").toString(), QString("ambiguous_target"));
        QCOMPARE(error.value("message").toString(), QString("Some targets matched more than one mod"));
        QCOMPARE(root.value("failed").toArray().size(), 1);
    }

    void test_outputSink_captureDocuments()
    {
        const QByteArray raw = captureStdout([] {
            Cli::OutputCapture capture;

            QJsonObject first;
            first.insert("n", 1);
            Cli::outputDocument(QJsonDocument(first));

            QJsonObject second;
            second.insert("n", 2);
            Cli::outputDocument(QJsonDocument(second));

            Cli::outputError("not_found", "nope", Cli::ExitCode::NotFound);

            // The output of the command is captured, not printed - the
            // terminal of the launcher stays clean when it serves the IPC.
            QCOMPARE(Cli::hasOutputSink(), true);
            QCOMPARE(capture.documents().size(), 3);
            QCOMPARE(capture.isRaw(), false);
            // The result is the last document - the envelope of the command.
            QCOMPARE(capture.result().value("ok").toBool(), false);
            QCOMPARE(capture.result().value("error").toObject().value("code").toString(), QString("not_found"));
            QCOMPARE(capture.documents().at(1).object().value("n").toInt(), 2);
        });

        // Nothing leaked to the standard output while the capture was alive.
        QVERIFY(raw.isEmpty());
    }

    void test_outputSink_captureRawText()
    {
        const QByteArray raw = captureStdout([] {
            Cli::OutputCapture capture;

            Cli::outputText("hello ");
            Cli::outputLine("world");

            QCOMPARE(capture.isRaw(), true);
            QCOMPARE(capture.result().value("text").toString(), QString("hello world\n"));
        });

        QVERIFY(raw.isEmpty());
    }

    void test_outputSink_captureTruncatesHugeText()
    {
        const QByteArray raw = captureStdout([] {
            Cli::OutputCapture capture;

            QString huge(1024 * 1024 + 4096, QLatin1Char('x'));
            Cli::outputText(huge);

            // The cap keeps a huge `log read` from ballooning the IPC frame.
            QCOMPARE(capture.result().value("truncated").toBool(), true);
            QCOMPARE(capture.result().value("text").toString().size(), qsizetype{ 1024 * 1024 });
        });

        QVERIFY(raw.isEmpty());
    }

    void test_outputSink_restoredAfterScope()
    {
        const QByteArray raw = captureStdout([] {
            {
                Cli::OutputCapture capture;
                Cli::outputText("captured");
            }

            // The scope is over - the standard output works again.
            QCOMPARE(Cli::hasOutputSink(), false);

            QJsonObject data;
            data.insert("value", 42);
            Cli::outputJson(QJsonDocument(data));
        });

        bool ok = false;
        const QJsonObject root = parseObject(raw, &ok);
        QVERIFY(ok);
        QCOMPARE(root.value("ok").toBool(), true);
        QCOMPARE(root.value("value").toInt(), 42);
    }

    void test_outputSink_nestedCaptureRestoresPrevious()
    {
        captureStdout([] {
            Cli::OutputCapture outer;
            {
                Cli::OutputCapture inner;
                QCOMPARE(Cli::hasOutputSink(), true);
                Cli::outputText("inner");
                QCOMPARE(inner.result().value("text").toString(), QString("inner"));
                QCOMPARE(outer.result().value("text").toString(), QString());
            }
            // The inner capture restored the outer one, not the stdout.
            QCOMPARE(Cli::hasOutputSink(), true);
            Cli::outputText("outer");
            QCOMPARE(outer.result().value("text").toString(), QString("outer"));
        });
    }
};

QTEST_GUILESS_MAIN(CliOutputTest)
#include "CliOutput_test.moc"
