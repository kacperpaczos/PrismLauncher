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

#include "cli/StatusCommands.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Application.h"
#include "BaseInstance.h"
#include "BuildConfig.h"
#include "InstanceList.h"
#include "cli/Output.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/MinecraftAccount.h"

namespace Cli {

ExitCode runStatusCommand(StatusContext& ctx, const Cmd::Status& cmd)
{
    QJsonArray instances;
    if (auto* list = ctx.app.instances()) {
        for (int i = 0; i < list->count(); ++i) {
            auto* instance = list->at(i);
            if (!instance) {
                continue;
            }
            QJsonObject entry;
            entry.insert(QStringLiteral("id"), instance->id());
            entry.insert(QStringLiteral("name"), instance->name());
            if (ctx.app.isHeadlessRun()) {
                // The command runs in a process of its own, so it cannot see
                // the instances that the launcher runs. The state is unknown,
                // not false.
                entry.insert(QStringLiteral("running"), QJsonValue());
            } else {
                entry.insert(QStringLiteral("running"), instance->isRunning());
            }
            instances.append(entry);
        }
    }

    QJsonArray accounts;
    if (auto* list = ctx.app.accounts()) {
        for (int i = 0; i < list->count(); ++i) {
            auto account = list->at(i);
            if (!account) {
                continue;
            }
            QJsonObject entry;
            entry.insert(QStringLiteral("id"), account->internalId());
            entry.insert(QStringLiteral("name"), account->profileName());
            entry.insert(QStringLiteral("type"), account->typeString());
            accounts.append(entry);
        }
    }

    QJsonObject out;
    out.insert(QStringLiteral("version"), BuildConfig.printableVersionString());
    out.insert(QStringLiteral("dataPath"), ctx.app.dataRoot());
    out.insert(QStringLiteral("instances"), instances);
    out.insert(QStringLiteral("accounts"), accounts);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace Cli
