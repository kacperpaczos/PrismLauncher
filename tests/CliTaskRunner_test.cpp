// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>
#include <QTimer>

#include "cli/TaskRunner.h"
#include "tasks/Task.h"

class FakeTask : public Task {
    Q_OBJECT

   public:
    enum class Outcome { Succeed, Fail, Hang, Abort };

    explicit FakeTask(Outcome outcome) : m_outcome(outcome) {}

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
    void test_nullTask()
    {
        const auto result = Cli::runTaskSync(nullptr, 1000);
        QVERIFY(!result.has_value());
        QVERIFY(result.error().contains("null"));
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
        QCOMPARE(result.error(), QString("boom"));
    }

    void test_timeout_taskThatCannotBeAborted()
    {
        // The task cannot be stopped, so it does not finish in the grace
        // period - the failure says that the operation did not finish.
        const auto result = Cli::runTaskSync(makeShared<FakeTask>(FakeTask::Outcome::Hang), 50, 200);
        QVERIFY(!result.has_value());
        QCOMPARE(result.error(), QString("operation_did_not_finish"));
        QVERIFY(Cli::taskDidNotFinish(result.error()));
    }

    void test_timeout_taskThatIsAborted()
    {
        // The task is asked to abort and it stops - the timeout is reported.
        const auto result = Cli::runTaskSync(makeShared<FakeTask>(FakeTask::Outcome::Abort), 50, 2000);
        QVERIFY(!result.has_value());
        QVERIFY(result.error().contains("timed out"));
        QVERIFY(!Cli::taskDidNotFinish(result.error()));
    }

    void test_timeout_waitsForTheTaskToFinish()
    {
        // The task finishes in the grace period - the timeout is reported and
        // not the failure of the task.
        const auto result = Cli::runTaskSync(makeShared<FakeTask>(FakeTask::Outcome::Abort), 50, 2000);
        QVERIFY(!result.has_value());
        QVERIFY(result.error().contains("timed out"));
    }
};

QTEST_GUILESS_MAIN(CliTaskRunnerTest)
#include "CliTaskRunner_test.moc"
