// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
 *  Copyright (C) 2023 Rachel Powers <508861+Ryex@users.noreply.github.com>
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

#pragma once

#include <QtNetwork>

#include "net/Request.h"
#include "tasks/ConcurrentTask.h"

// Those are included so that they are also included by anyone using NetJob
#include "net/HttpMetaCache.h"

class NetJob : public ConcurrentTask {
    Q_OBJECT

   public:
    // TODO: delete
    using Ptr = shared_qobject_ptr<NetJob>;

    explicit NetJob(const QString& jobName, QNetworkAccessManager* network, int maxConcurrent = -1);
    ~NetJob() override = default;

    auto size() const -> int;

    auto canAbort() const -> bool override;
    auto addNetAction(const Net::Request::Ptr& action) -> bool;

    auto getFailedActions() -> QList<Net::Request*>;
    auto getFailedFiles() -> QList<QString>;
    void setAskRetry(bool askRetry);

    /// Whether the job asks the user whether a failed request should be retried.
    bool asksRetry() const { return m_askRetry && !isUnattended(); }
    bool shouldAskRetry(bool headlessRun) const { return !headlessRun && asksRetry(); }

    /**
     * @brief whether the jobs that are created ask for a retry
     *
     * The jobs of the GUI ask the user whether a failed request should be
     * retried - a dialog cannot be answered in a headless run, so the CLI
     * turns the default off for the duration of its operations. The default
     * is on, so that the GUI keeps its behaviour.
     */
    static bool defaultAskRetry();

    /// @brief set the default for the jobs that are created afterwards
    static void setDefaultAskRetry(bool askRetry);

   protected slots:
    void emitFailed(QString reason) override;
    void executeNextSubTask() override;

   protected:
    void updateState() override;
    bool isOnline();
    virtual bool isHeadlessRun() const;
    virtual bool askRetry(const QString& reason);

   private:
    QNetworkAccessManager* m_network;

    int m_try = 1;
    int m_maxTries = 3;
    bool m_askRetry = true;

    // The default of the `m_askRetry` of the jobs that are created.
    static bool s_defaultAskRetry;
};
