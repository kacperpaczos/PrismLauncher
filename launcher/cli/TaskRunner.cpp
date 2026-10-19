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

#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

namespace Cli {

Result<void, QString> runTaskSync(Task::Ptr task, int timeoutMs)
{
    if (!task) {
        return std::unexpected(QStringLiteral("cannot run a null task"));
    }

    QEventLoop loop;
    bool done = false;

    QObject::connect(task.get(), &Task::finished, &loop, [&loop, &done] {
        done = true;
        loop.quit();
    });

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&loop, &timedOut, &task] {
        timedOut = true;
        if (task->canAbort()) {
            task->abort();
        }
        loop.quit();
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
    task->start();
    if (!done) {
        loop.exec();
    }
    timeoutTimer.stop();

    // clear the progress line
    std::fprintf(stderr, "\r\033[K");
    std::fflush(stderr);

    if (timedOut) {
        return std::unexpected(QStringLiteral("task timed out after %1 ms").arg(timeoutMs));
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
