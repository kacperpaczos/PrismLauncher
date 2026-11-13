// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QSignalSpy>
#include <QTest>

#include "net/NetJob.h"

namespace {
class NetJobAbortRequest : public Net::Request {
   public:
    NetJobAbortRequest(bool acceptsAbort, bool finishesOnAbort)
        : Net::Request(QUrl("http://localhost/unused")), m_acceptsAbort(acceptsAbort), m_finishesOnAbort(finishesOnAbort)
    {}
    bool abort() override
    {
        if (m_finishesOnAbort) {
            emitAborted();
        }
        return m_acceptsAbort;
    }
    void complete() { emitSucceeded(); }

   protected:
    void executeTask() override {}

   private:
    bool m_acceptsAbort;
    bool m_finishesOnAbort;
};

class AbortNetJob : public NetJob {
   public:
    AbortNetJob() : NetJob("abort test", nullptr) {}
    int interactiveFailures = 0;

   protected:
    void emitFailed(QString reason) override
    {
        ++interactiveFailures;
        NetJob::emitFailed(reason);
    }
};
}  // namespace

class NetJobTest : public QObject {
    Q_OBJECT

   private slots:
    void test_idleAbortFinishes()
    {
        AbortNetJob job;
        QSignalSpy finished(&job, &Task::finished);
        job.start();
        QVERIFY(job.abort());
        QCOMPARE(job.getState(), Task::State::AbortedByUser);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(job.interactiveFailures, 0);
        QCoreApplication::processEvents();
        QCOMPARE(finished.count(), 1);
    }

    void test_activeAbort_data()
    {
        QTest::addColumn<bool>("acceptsAbort");
        QTest::addColumn<bool>("finishesOnAbort");
        QTest::newRow("stops") << true << true;
        QTest::newRow("pending") << true << false;
        QTest::newRow("refuses") << false << false;
        QTest::newRow("refuses-after-stopping") << false << true;
    }

    void test_activeAbort()
    {
        QFETCH(bool, acceptsAbort);
        QFETCH(bool, finishesOnAbort);
        AbortNetJob job;
        job.setAskRetry(true);
        auto request = makeShared<NetJobAbortRequest>(acceptsAbort, finishesOnAbort);
        QVERIFY(job.addNetAction(request));
        job.start();
        QTRY_VERIFY(request->isRunning());
        QSignalSpy finished(&job, &Task::finished);
        QCOMPARE(job.abort(), acceptsAbort);
        if (!finishesOnAbort) {
            QVERIFY(job.isRunning());
            QCOMPARE(finished.count(), 0);
            request->complete();
        }
        QCOMPARE(job.getState(), acceptsAbort ? Task::State::AbortedByUser : Task::State::Failed);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(job.interactiveFailures, 0);
    }

    void cleanup()
    {
        // The default is global - every test restores it.
        NetJob::setDefaultAskRetry(true);
    }

    void test_theDefaultIsOn()
    {
        // The GUI asks the user whether a failed request should be retried.
        QCOMPARE(NetJob::defaultAskRetry(), true);
    }

    void test_aJobUsesTheDefault()
    {
        NetJob::setDefaultAskRetry(false);
        NetJob job("test", nullptr);
        QCOMPARE(job.asksRetry(), false);

        NetJob::setDefaultAskRetry(true);
        NetJob other("test", nullptr);
        QCOMPARE(other.asksRetry(), true);
    }

    void test_anExplicitValueWins()
    {
        NetJob::setDefaultAskRetry(false);
        NetJob job("test", nullptr);
        QCOMPARE(job.asksRetry(), false);

        // The job itself decides - the default does not change it any more.
        job.setAskRetry(true);
        QCOMPARE(job.asksRetry(), true);
    }
};

QTEST_GUILESS_MAIN(NetJobTest)
#include "NetJob_test.moc"
