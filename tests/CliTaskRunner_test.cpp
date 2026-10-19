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
    enum class Outcome { Succeed, Fail, Hang };

    explicit FakeTask(Outcome outcome) : m_outcome(outcome) {}

    void executeTask() override
    {
        if (m_outcome == Outcome::Hang) {
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

    void test_timeout()
    {
        const auto result = Cli::runTaskSync(makeShared<FakeTask>(FakeTask::Outcome::Hang), 50);
        QVERIFY(!result.has_value());
        QVERIFY(result.error().contains("timed out"));
    }
};

QTEST_GUILESS_MAIN(CliTaskRunnerTest)
#include "CliTaskRunner_test.moc"
