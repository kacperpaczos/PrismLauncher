// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QPointer>
#include <QTest>
#include <QTimer>

#include "cli/CommandUtils.h"

#include "cli/TaskRunner.h"
#include "tasks/Task.h"

class FakeTask : public Task {
    Q_OBJECT

   public:
    enum class Outcome { Succeed, Fail, Hang, Abort };

    explicit FakeTask(Outcome outcome) : m_outcome(outcome) {}

    void complete() { emitSucceeded(); }

    void executeTask() override
    {
        if (m_outcome == Outcome::Hang) {
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
