// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
 *  Copyright (c) 2023 Rachel Powers <508861+Ryex@users.noreply.github.com>
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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */
#include "ConcurrentTask.h"

#include <QDebug>
#include "tasks/Task.h"

ConcurrentTask::ConcurrentTask(QString task_name, int max_concurrent) : Task(), m_total_max_size(max_concurrent)
{
    setObjectName(task_name);
}

ConcurrentTask::~ConcurrentTask()
{
    for (auto task : m_doing) {
        if (task)
            task->disconnect(this);
    }
}

auto ConcurrentTask::getStepProgress() const -> TaskStepProgressList
{
    return m_task_progress.values();
}

bool ConcurrentTask::addTask(Task::Ptr task)
{
    if (!task || !task->setUnattended(isUnattended())) {
        return false;
    }
    m_queue.append(task);
    return true;
}

bool ConcurrentTask::setUnattended(bool unattended)
{
    if (isRunning()) {
        return Task::setUnattended(unattended);
    }
    for (const auto& task : m_queue) {
        if (!task->setUnattended(unattended)) {
            return false;
        }
    }
    return Task::setUnattended(unattended);
}

void ConcurrentTask::executeTask()
{
    m_aborting = false;
    m_abortFailed = false;
    for (auto i = 0; i < m_total_max_size; i++)
        QMetaObject::invokeMethod(this, &ConcurrentTask::executeNextSubTask, Qt::QueuedConnection);
}

bool ConcurrentTask::abort()
{
    if (!isRunning() || m_aborting) {
        return !m_abortFailed;
    }
    m_aborting = true;
    m_abortInProgress = true;
    m_queue.clear();

    // abort() can synchronously emit a child's terminal signals and remove it.
    const auto active = m_doing.values();
    for (const auto& task : active) {
        if (!task->isRunning()) {
            subTaskFinished(task, TaskStepState::Failed);
            continue;
        }
        disconnect(task.get(), &Task::aborted, this, nullptr);
        connect(task.get(), &Task::finished, this, [this, task] {
            if (m_doing.contains(task.get())) {
                subTaskFinished(task, TaskStepState::Failed);
            }
        });
        if (!task->abort()) {
            m_abortFailed = true;
        }
    }
    m_abortInProgress = false;
    finishAbort();
    return !m_abortFailed;
}

void ConcurrentTask::finishAbort()
{
    if (m_abortInProgress || !m_doing.isEmpty()) {
        return;
    }
    if (m_abortFailed) {
        Task::emitFailed(tr("Failed to abort all running tasks."));
    } else {
        emitAborted();
    }
}

void ConcurrentTask::clear()
{
    Q_ASSERT(!isRunning());

    m_done.clear();
    m_failed.clear();
    m_queue.clear();
    m_task_progress.clear();

    m_progress = 0;
}

void ConcurrentTask::executeNextSubTask()
{
    if (!isRunning() || m_aborting) {
        return;
    }
    if (m_doing.count() >= m_total_max_size) {
        return;
    }
    if (m_queue.isEmpty()) {
        if (m_doing.isEmpty()) {
            if (m_failed.isEmpty()) {
                emitSucceeded();
            } else if (m_failed.count() == 1) {
                auto task = m_failed.keys().first();
                auto reason = task->failReason();
                if (reason.isEmpty()) {  // clearly a bug somewhere
                    reason = tr("Task failed");
                }
                emitFailed(reason);
            } else {
                QStringList failReason;
                for (auto t : m_failed) {
                    auto reason = t->failReason();
                    if (!reason.isEmpty()) {
                        failReason << reason;
                    }
                }
                if (failReason.isEmpty()) {
                    emitFailed(tr("Multiple subtasks failed"));
                } else {
                    emitFailed(tr("Multiple subtasks failed\n%1").arg(failReason.join("\n")));
                }
            }
        }
        return;
    }

    startSubTask(m_queue.dequeue());
}

void ConcurrentTask::startSubTask(Task::Ptr next)
{
    if (!next->setUnattended(isUnattended())) {
        // Do not cancel a shared task belonging to an unrelated operation.
        m_abortFailed = true;
        abort();
        return;
    }
    connect(next.get(), &Task::succeeded, this, [this, next]() { subTaskSucceeded(next); });
    connect(next.get(), &Task::failed, this, [this, next](QString msg) {
        if (m_aborting) {
            subTaskFinished(next, TaskStepState::Failed);
        } else {
            subTaskFailed(next, msg);
        }
    });
    // this should never happen but if it does, it's better to fail the task than get stuck
    connect(next.get(), &Task::aborted, this, [this, next] { subTaskFailed(next, "Aborted"); });

    connect(next.get(), &Task::status, this, [this, next](QString msg) { subTaskStatus(next, msg); });
    connect(next.get(), &Task::details, this, [this, next](QString msg) { subTaskDetails(next, msg); });
    connect(next.get(), &Task::stepProgress, this, &ConcurrentTask::stepProgress);

    connect(next.get(), &Task::progress, this, [this, next](qint64 current, qint64 total) { subTaskProgress(next, current, total); });

    m_doing.insert(next.get(), next);

    auto task_progress = std::make_shared<TaskStepProgress>(next->getUid());
    m_task_progress.insert(next->getUid(), task_progress);

    updateState();

    QMetaObject::invokeMethod(
        this,
        [this, next] {
            // Metadata loaders can be shared by more than one composite. Another
            // owner may have started or completed it since we queued this callback.
            if (isRunning() && m_doing.contains(next.get()) && !next->isRunning()) {
                next->start();
            }
        },
        Qt::QueuedConnection);
}

void ConcurrentTask::subTaskFinished(Task::Ptr task, TaskStepState state)
{
    m_done.insert(task.get(), task);
    (state == TaskStepState::Succeeded ? m_succeeded : m_failed).insert(task.get(), task);

    m_doing.remove(task.get());

    auto task_progress = *m_task_progress.value(task->getUid());
    task_progress.state = state;
    m_task_progress.remove(task->getUid());

    disconnect(task.get(), 0, this, 0);

    emit stepProgress(task_progress);
    updateState();
    if (m_aborting) {
        finishAbort();
    } else {
        QMetaObject::invokeMethod(this, &ConcurrentTask::executeNextSubTask, Qt::QueuedConnection);
    }
}

void ConcurrentTask::subTaskSucceeded(Task::Ptr task)
{
    subTaskFinished(task, TaskStepState::Succeeded);
}

void ConcurrentTask::subTaskFailed(Task::Ptr task, [[maybe_unused]] const QString& msg)
{
    subTaskFinished(task, TaskStepState::Failed);
}

void ConcurrentTask::subTaskStatus(Task::Ptr task, const QString& msg)
{
    auto task_progress = m_task_progress.value(task->getUid());
    task_progress->status = msg;
    task_progress->state = TaskStepState::Running;

    emit stepProgress(*task_progress);

    if (totalSize() == 1) {
        setStatus(msg);
    }
}

void ConcurrentTask::subTaskDetails(Task::Ptr task, const QString& msg)
{
    auto task_progress = m_task_progress.value(task->getUid());
    task_progress->details = msg;
    task_progress->state = TaskStepState::Running;

    emit stepProgress(*task_progress);

    if (totalSize() == 1) {
        setDetails(msg);
    }
}

void ConcurrentTask::subTaskProgress(Task::Ptr task, qint64 current, qint64 total)
{
    auto task_progress = m_task_progress.value(task->getUid());

    task_progress->update(current, total);

    emit stepProgress(*task_progress);
    updateState();

    if (totalSize() == 1) {
        setProgress(task_progress->current, task_progress->total);
    }
}

void ConcurrentTask::updateState()
{
    if (totalSize() > 1) {
        setProgress(m_done.count(), totalSize());
        setStatus(tr("Executing %1 task(s) (%2 out of %3 are done)")
                      .arg(QString::number(m_doing.count()), QString::number(m_done.count()), QString::number(totalSize())));
    } else {
        QString status = tr("Please wait...");
        if (m_queue.size() > 0) {
            status = tr("Waiting for a task to start...");
        } else if (m_doing.size() > 0) {
            status = tr("Executing 1 task:");
        } else if (m_done.size() > 0) {
            status = tr("Task finished.");
        }
        setStatus(status);
    }
}
