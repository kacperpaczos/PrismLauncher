// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QFile>
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
};

QTEST_GUILESS_MAIN(CliOutputTest)
#include "CliOutput_test.moc"
