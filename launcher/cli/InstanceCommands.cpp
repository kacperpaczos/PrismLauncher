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

#include "cli/InstanceCommands.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "Application.h"
#include "InstanceList.h"
#include "cli/InstanceSupport.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"

namespace Cli {

ExitCode runInstanceCommand(ModContext& ctx, const Cmd::InstanceList& cmd)
{
    auto* instances = ctx.app.instances();
    if (!instances) {
        return outputError(QStringLiteral("no_instances"), QStringLiteral("The launcher has no instance list"), ExitCode::Generic);
    }

    QJsonArray list;
    for (int i = 0; i < instances->count(); ++i) {
        auto* instance = instances->at(i);
        if (cmd.group.has_value() && instances->getInstanceGroup(instance->id()) != cmd.group.value()) {
            continue;
        }
        loadProfile(instance);
        list.append(instanceToJson(instance, instances));
    }

    QJsonObject out;
    out.insert(QStringLiteral("total"), static_cast<int>(list.size()));
    out.insert(QStringLiteral("instances"), list);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runInstanceCommand(ModContext& ctx, const Cmd::InstanceInfo& cmd)
{
    auto* instance = resolveInstance(ctx, cmd.id);
    if (!instance) {
        return outputError(QStringLiteral("instance_not_found"),
                           QStringLiteral("No instance with the id '%1' was found").arg(cmd.id),
                           ExitCode::NotFound);
    }
    auto* instances = ctx.app.instances();

    loadProfile(instance);

    auto object = instanceToJson(instance, instances);
    object.insert(QStringLiteral("game_root"), instance->gameRoot());
    object.insert(QStringLiteral("instance_root"), instance->instanceRoot());
    object.insert(QStringLiteral("mods_root"), instance->modsRoot());
    object.insert(QStringLiteral("uuid"), instance->uuid());
    object.insert(QStringLiteral("instance_type"), instance->instanceType());

    QJsonArray components;
    if (auto* profile = instance->getPackProfile(); profile) {
        for (int i = 0; i < profile->rowCount(); ++i) {
            auto component = profile->getComponent(static_cast<size_t>(i));
            if (!component) {
                continue;
            }
            QJsonObject entry;
            entry.insert(QStringLiteral("uid"), component->getID());
            entry.insert(QStringLiteral("name"), component->getName());
            entry.insert(QStringLiteral("version"), component->getVersion());
            entry.insert(QStringLiteral("enabled"), component->isEnabled());
            components.append(entry);
        }
    }
    object.insert(QStringLiteral("components"), components);

    QJsonObject out;
    out.insert(QStringLiteral("instance"), object);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace Cli
