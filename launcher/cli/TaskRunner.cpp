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

#include "cli/TaskRunner.h"

#include <cstdio>
#include <vector>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

namespace Cli {
namespace {

/**
 * The tasks that did not finish in the grace period after a timeout.
 *
 * A task that is still running must not be deleted - its callbacks would
 * point to freed memory and the process would crash. The list keeps such
 * tasks alive; when a task eventually finishes on its own, it is released
 * through its finished signal. The list itself is not destroyed on purpose:
 * the tasks that never finish are gone when the process exits anyway.
 */
std::vector<Task::Ptr>& unfinishedTasks()
{
    static auto* tasks = new std::vector<Task::Ptr>();
    return *tasks;
}

}  // namespace

Result<void, QString> runTaskSync(Task::Ptr task, int timeoutMs, int gracePeriodMs)
{
    if (!task) {
        return std::unexpected(QStringLiteral("cannot run a null task"));
    }

    // A task that was started by the launcher itself (the resolution of the
    // components of an instance for example) is only waited for - starting it
    // again would abort the process.
    const auto state = task->getState();
    if (state != Task::State::Inactive && state != Task::State::Running) {
        // The task is already finished, so there is nothing to wait for.
        if (task->wasSuccessful()) {
            return {};
        }
        auto reason = task->failReason();
        if (reason.isEmpty()) {
            reason = QStringLiteral("task failed");
        }
        return std::unexpected(reason);
    }

    QEventLoop loop;
    bool done = false;
    bool timedOut = false;
    bool didNotFinish = false;

    QObject::connect(task.get(), &Task::finished, &loop, [&loop, &done] {
        done = true;
        loop.quit();
    });

    QTimer graceTimer;
    graceTimer.setSingleShot(true);
    QObject::connect(&graceTimer, &QTimer::timeout, &loop, [&loop, &didNotFinish] {
        didNotFinish = true;
        loop.quit();
    });

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&loop, &timedOut, &task, &graceTimer, gracePeriodMs] {
        timedOut = true;
        // The task is asked to stop - the result of the command is not needed
        // any more, but the task may not be able to stop right away. abort()
        // is safe to call even when the task says that it cannot be aborted:
        // it is a no-op then, and some tasks stop anyway.
        task->abort();
        // The task gets a grace period to finish - the event loop keeps
        // running until it does or until the grace period is over.
        graceTimer.start(gracePeriodMs);
    });

    // one-line, throttled (<= 2/s) progress report on stderr
    QElapsedTimer progressClock;
    progressClock.start();
    qint64 lastReportMs = -1000;
    QObject::connect(task.get(), &Task::progress, &loop, [&progressClock, &lastReportMs, &task](qint64 current, qint64 total) {
        if (progressClock.elapsed() - lastReportMs < 500) {
            return;
        }
        lastReportMs = progressClock.elapsed();

        const QString status = task->getStatus();
        if (total > 0) {
            std::fprintf(stderr, "\r%s: %lld/%lld   ", qPrintable(status), static_cast<long long>(current),
                         static_cast<long long>(total));
        } else if (!status.isEmpty()) {
            std::fprintf(stderr, "\r%s   ", qPrintable(status));
        } else {
            return;
        }
        std::fflush(stderr);
    });

    timeoutTimer.start(timeoutMs);
    if (state == Task::State::Inactive) {
        task->start();
    }
    if (!done) {
        loop.exec();
    }
    timeoutTimer.stop();
    graceTimer.stop();

    // clear the progress line
    std::fprintf(stderr, "\r\033[K");
    std::fflush(stderr);

    if (didNotFinish) {
        // The task is still running - it is kept alive, so that its callbacks
        // stay valid until the process exits.
        unfinishedTasks().push_back(task);
        // When the task eventually finishes on its own, the reaper releases
        // it again. The reference of the reaper is the last one, so dropping
        // it deletes the task through its deferred deleter - from the event
        // loop, never inside the emission of its own finished signal.
        QObject::connect(task.get(), &Task::finished, task.get(), [task] { std::erase(unfinishedTasks(), task); });
        return std::unexpected(QString::fromLatin1(g_TaskDidNotFinish));
    }

    if (timedOut) {
        // The task stopped in the grace period, but the operation was not
        // completed - the command reports that it was cancelled, not that
        // it failed for the reason of the task.
        return std::unexpected(QStringLiteral("the operation was cancelled after the timeout (%1 ms)").arg(timeoutMs));
    }

    if (!task->wasSuccessful()) {
        auto reason = task->failReason();
        if (reason.isEmpty()) {
            reason = QStringLiteral("task failed");
        }
        return std::unexpected(reason);
    }

    return {};
}

}  // namespace Cli
