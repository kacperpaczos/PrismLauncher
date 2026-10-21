// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>

#include "net/NetJob.h"

class NetJobTest : public QObject {
    Q_OBJECT

   private slots:
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
