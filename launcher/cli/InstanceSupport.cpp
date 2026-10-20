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

#include "cli/InstanceSupport.h"

#include <QDateTime>
#include <QJsonValue>

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"
#include "cli/TaskRunner.h"
#include "minecraft/Component.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "modplatform/ModIndex.h"
#include "net/Mode.h"

namespace Cli {

MinecraftInstance* resolveInstance(ModContext& ctx, const QString& id)
{
    if (id.isEmpty()) {
        return nullptr;
    }
    auto* instances = ctx.app.instances();
    if (!instances) {
        return nullptr;
    }
    return instances->getInstanceById(id);
}

ExitCode checkInstanceRunning(MinecraftInstance* instance, const QString& instanceId, const QString& action, const QString& hint)
{
    if (!instance->isRunning()) {
        return ExitCode::OK;
    }
    auto message = QStringLiteral("The instance '%1' is running, refusing to %2").arg(instanceId, action);
    if (!hint.isEmpty()) {
        message += QStringLiteral(" (%1)").arg(hint);
    }
    return outputError(QStringLiteral("instance_running"), message, ExitCode::Conflict);
}

bool loadComponentProfile(MinecraftInstance* instance)
{
    auto* profile = instance->getPackProfile();
    if (!profile) {
        return false;
    }
    if (!profile->getComponentVersion("net.minecraft").isEmpty()) {
        // The profile was already loaded.
        return true;
    }
    if (auto result = profile->reload(Net::Mode::Offline); !result) {
        qWarning() << "Could not load the component profile of the instance:" << result.error();
        return false;
    }
    return true;
}

bool loadProfile(MinecraftInstance* instance)
{
    auto* profile = instance->getPackProfile();
    if (!profile) {
        return false;
    }
    if (auto result = profile->reload(Net::Mode::Offline); !result) {
        qWarning() << "Could not load the component profile of the instance:" << result.error();
        return false;
    }
    if (auto task = profile->getCurrentTask(); task) {
        // The resolution needs the metadata of the components - without it the
        // versions of the components are not known, but the profile itself is
        // usable.
        if (auto result = runTaskSync(task); !result) {
            qWarning() << "Could not resolve the component profile of the instance:" << result.error();
        }
    }
    return true;
}

/**
 * The version of a component.
 *
 * The cached version of a component is only known after the components were
 * resolved (it needs the metadata of the components), so the version that is
 * stored in the profile is used as a fallback.
 */
QString componentVersion(const shared_qobject_ptr<Component>& component)
{
    if (!component) {
        return QString();
    }
    const auto cached = component->getVersion();
    return cached.isEmpty() ? component->m_version : cached;
}

std::optional<LoaderInfo> findLoader(PackProfile* profile)
{
    if (!profile) {
        return std::nullopt;
    }
    for (auto it = Component::KNOWN_MODLOADERS.constBegin(); it != Component::KNOWN_MODLOADERS.constEnd(); ++it) {
        auto component = profile->getComponent(it.key());
        if (component && component->isEnabled()) {
            return LoaderInfo{ .name = ModPlatform::getModLoaderAsString(it.value().type),
                               .uid = it.key(),
                               .version = componentVersion(component) };
        }
    }
    return std::nullopt;
}

namespace {

QJsonValue dateToJson(qint64 milliseconds)
{
    if (milliseconds <= 0) {
        return QJsonValue(QJsonValue::Null);
    }
    return QDateTime::fromMSecsSinceEpoch(milliseconds, Qt::UTC).toString(Qt::ISODate);
}

}  // namespace

QJsonObject instanceToJson(MinecraftInstance* instance, InstanceList* instances)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), instance->id());
    object.insert(QStringLiteral("name"), instance->name());
    object.insert(QStringLiteral("path"), instance->instanceRoot());
    object.insert(QStringLiteral("group"), instances ? instances->getInstanceGroup(instance->id()) : QString());

    auto* profile = instance->getPackProfile();
    // The cached version of the component is only known after the components
    // were resolved - a freshly created instance has the requested version
    // only.
    const auto mcVersion = profile ? componentVersion(profile->getComponent(QStringLiteral("net.minecraft"))) : QString();
    object.insert(QStringLiteral("mc_version"), mcVersion.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(mcVersion));

    const auto loader = findLoader(profile);
    object.insert(QStringLiteral("loader"), loader ? QJsonValue(loader->name) : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("loader_uid"), loader ? QJsonValue(loader->uid) : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("loader_version"), loader ? QJsonValue(loader->version) : QJsonValue(QJsonValue::Null));

    object.insert(QStringLiteral("icon_key"), instance->iconKey());
    object.insert(QStringLiteral("last_played"), dateToJson(instance->lastLaunch()));
    object.insert(QStringLiteral("total_time_played"), static_cast<qint64>(instance->totalTimePlayed()));

    const auto managedPack = instance->getManagedPackName();
    object.insert(QStringLiteral("managed_pack"), managedPack.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(managedPack));
    // The running state of an instance lives in the process of the launcher -
    // a separate CLI process never sees a running instance.
    object.insert(QStringLiteral("running"), QJsonValue(QJsonValue::Null));

    return object;
}

}  // namespace Cli
