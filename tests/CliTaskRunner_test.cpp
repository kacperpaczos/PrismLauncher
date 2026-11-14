// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QTest>
#include <QTimer>
#include <array>

#include "cli/CommandUtils.h"
#include "cli/ModCommands.h"
#include "cli/Output.h"
#include "net/NetJob.h"

#include "cli/TaskRunner.h"
#include "tasks/Task.h"

namespace {
class CliDownloadFailureRequest : public Net::Request {
   public:
    CliDownloadFailureRequest() : Net::Request(QUrl("http://localhost/unused")) {}

   protected:
    void executeTask() override { emitFailed("download failed"); }
};
}  // namespace

class FakeTask : public Task {
    Q_OBJECT

   public:
    enum class Outcome { Succeed, Fail, Hang, Abort, AcceptWithoutFinishing };

    explicit FakeTask(Outcome outcome) : m_outcome(outcome) {}

    void complete() { emitSucceeded(); }
    bool abort() override { return m_outcome == Outcome::AcceptWithoutFinishing ? true : Task::abort(); }

    void executeTask() override
    {
        if (m_outcome == Outcome::Hang || m_outcome == Outcome::AcceptWithoutFinishing) {
            // The task never finishes and it cannot be aborted.
            return;
        }
        if (m_outcome == Outcome::Abort) {
            // The task stops when it is asked to abort.
            setAbortable(true);
            return;
        }
        QTimer::singleShot(0, this, [this] {
            if (m_outcome == Outcome::Succeed) {
                emitSucceeded();
            } else {
                emitFailed(QStringLiteral("boom"));
            }
        });
    }

   private:
    Outcome m_outcome;
};

class CliTaskRunnerTest : public QObject {
    Q_OBJECT

   private slots:
    void test_modInstallDownloadFailureIsOneEnvelope()
    {
        auto job = makeShared<NetJob>("failed mod download", nullptr);
        job->setAskRetry(true);
        // Simulate download failure without Application or external network I/O.
        auto request = makeShared<CliDownloadFailureRequest>();
        QVERIFY(job->addNetAction(request));
        auto download = makeShared<ConcurrentTask>("mod install");
        QVERIFY(download->addTask(job));
        auto failed = std::make_shared<QJsonArray>();
        connect(job.get(), &Task::failed, download.get(), [failed](const QString& reason) {
            failed->append(QJsonObject{ { "slug", "test-mod" }, { "code", "download" }, { "reason", reason } });
        });
        Cli::OutputCapture capture;
        QElapsedTimer timer;
        timer.start();
        // Keep the deadline well beyond the responsiveness bound below.
        const auto code = Cli::runModInstallDownload(download, failed, 30000);
        QCOMPARE(code, Cli::ExitCode::Network);
        QVERIFY(timer.elapsed() < 5000);
        QCOMPARE(capture.documents().size(), std::size_t{ 1 });
        const auto envelope = capture.documents().front().object();
        QCOMPARE(envelope.value("ok").toBool(), false);
        QCOMPARE(envelope.value("error").toObject().value("code").toString(), QString("download_failed"));
        QCOMPARE(envelope.value("failed").toArray().size(), 1);
        QVERIFY(job->isUnattended());
        QVERIFY(request->isUnattended());
        QVERIFY(!job->asksRetry());
    }

    void test_acceptedAbortStillRequiresTerminalAcknowledgement()
    {
        auto child = makeShared<FakeTask>(FakeTask::Outcome::AcceptWithoutFinishing);
        auto parent = makeShared<ConcurrentTask>();
        QVERIFY(parent->addTask(child));
        const auto result = Cli::runTaskSync(parent, 200, 200);
        QVERIFY(!result);
        QCOMPARE(result.error().code, Task::FailureCode::Incomplete);
        QVERIFY(parent->isRunning());
        // A true return only accepts the request; retain ownership until completion.
        child->complete();
        QCOMPARE(parent->getState(), Task::State::AbortedByUser);
    }

    void test_projectErrorUsesCode()
    {
        const std::array cases{ Cli::TaskError{ Task::FailureCode::Failed, "the project was not found" },
                                Cli::TaskError{ Task::FailureCode::NotFound, "arbitrary message" },
                                Cli::TaskError{ Task::FailureCode::Incomplete, "still running" },
                                Cli::TaskError{ Task::FailureCode::PolicyConflict, "incompatible policy" } };
        const std::array codes{ Cli::ExitCode::Network, Cli::ExitCode::NotFound, Cli::ExitCode::Generic, Cli::ExitCode::Generic };
        const std::array names{ "network_error", "project_not_found", "operation_did_not_finish", "task_policy_conflict" };
        for (std::size_t i = 0; i < cases.size(); ++i) {
            Cli::OutputCapture output;
            QCOMPARE(Cli::reportProjectError(cases[i]), codes[i]);
            QCOMPARE(output.documents().size(), std::size_t{ 1 });
            QCOMPARE(output.result().value("error").toObject().value("code").toString(), QString(names[i]));
        }
    }

    void test_cancelledTaskReturnsItsCode()
    {
        auto task = makeShared<FakeTask>(FakeTask::Outcome::Abort);
        task->start();
        QVERIFY(task->abort());
        const auto result = Cli::runTaskSync(task);
        QVERIFY(!result);
        QCOMPARE(result.error().code, Task::FailureCode::Cancelled);
    }

    void test_finishedTimeoutTaskIsReleased()
    {
        auto task = makeShared<FakeTask>(FakeTask::Outcome::Hang);
        QPointer<FakeTask> pointer(task.get());
        QWeakPointer<FakeTask> weak(task);
        const auto result = Cli::runTaskSync(task, 1, 1);
        QVERIFY(!result);
        QCOMPARE(result.error().code, Task::FailureCode::Incomplete);
        task.reset();
        QVERIFY(!weak.isNull());
        pointer->complete();
        QVERIFY(weak.isNull());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(pointer.isNull());
    }

    void test_messageDoesNotDetermineCode()
    {
        const Cli::TaskError failure{ Task::FailureCode::Failed, "operation_did_not_finish" };
        const auto mapped = Cli::taskFailure("download_failed", failure, Cli::ExitCode::Network);
        QCOMPARE(mapped.code, QString("download_failed"));
        QCOMPARE(mapped.exitCode, Cli::ExitCode::Network);
        const Cli::TaskError incomplete{ Task::FailureCode::Incomplete, "a completely different message" };
        const auto adjusted = Cli::taskFailure("download_failed", incomplete, Cli::ExitCode::Network);
        QCOMPARE(adjusted.code, QString("operation_did_not_finish"));
        QCOMPARE(adjusted.exitCode, Cli::ExitCode::Generic);
    }

    void test_nullTask()
    {
        const auto result = Cli::runTaskSync(nullptr, 1000);
        QVERIFY(!result.has_value());
        QVERIFY(result.error().message.contains("null"));
        QCOMPARE(result.error().code, Task::FailureCode::InvalidTask);
    }

    void test_success()
    {
        const auto result = Cli::runTaskSync(makeShared<FakeTask>(FakeTask::Outcome::Succeed), 5000);
        QVERIFY(result.has_value());
    }

    void test_failure()
    {
        const auto result = Cli::runTaskSync(makeShared<FakeTask>(FakeTask::Outcome::Fail), 5000);
        QVERIFY(!result.has_value());
        QCOMPARE(result.error().message, QString("boom"));
        QCOMPARE(result.error().code, Task::FailureCode::Failed);
    }

    void test_timeout_taskThatCannotBeAborted()
    {
        // The task cannot be stopped, so it does not finish in the grace
        // period - the failure says that the operation did not finish.
        const auto result = Cli::runTaskSync(makeShared<FakeTask>(FakeTask::Outcome::Hang), 50, 200);
        QVERIFY(!result.has_value());
        QCOMPARE(result.error().code, Task::FailureCode::Incomplete);
    }

    void test_timeout_taskThatIsAborted()
    {
        // The task is asked to abort and it stops - the cancellation is reported.
        const auto result = Cli::runTaskSync(makeShared<FakeTask>(FakeTask::Outcome::Abort), 50, 2000);
        QVERIFY(!result.has_value());
        QVERIFY(result.error().message.contains("cancelled after the timeout"));
        QCOMPARE(result.error().code, Task::FailureCode::Timeout);
    }

    void test_timeout_waitsForTheTaskToFinish()
    {
        // The task finishes in the grace period - the cancellation is reported and
        // not the failure of the task.
        const auto result = Cli::runTaskSync(makeShared<FakeTask>(FakeTask::Outcome::Abort), 50, 2000);
        QVERIFY(!result.has_value());
        QVERIFY(result.error().message.contains("cancelled after the timeout"));
    }
};

QTEST_GUILESS_MAIN(CliTaskRunnerTest)
#include "CliTaskRunner_test.moc"
