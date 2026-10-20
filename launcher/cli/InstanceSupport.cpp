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

#include <algorithm>
#include <vector>

#include <QDateTime>
#include <QJsonValue>
#include <QMap>

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"
#include "Version.h"
#include "cli/TaskRunner.h"
#include "meta/Index.h"
#include "meta/Version.h"
#include "meta/VersionList.h"
#include "minecraft/Component.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "modplatform/ModIndex.h"
#include "net/Mode.h"

namespace Cli {
namespace {

/// Upper bound for the download of the metadata of a component.
constexpr int g_MetadataTimeoutMs = 300000;

}  // namespace

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

const QMap<QString, QString>& loaderUids()
{
    static const QMap<QString, QString> uids = {
        { "fabric", "net.fabricmc.fabric-loader" },
        { "forge", "net.minecraftforge" },
        { "neoforge", "net.neoforged" },
        { "quilt", "org.quiltmc.quilt-loader" },
    };
    return uids;
}

std::vector<QString> versionsForMinecraft(Meta::VersionList* list, const QString& mcVersion, const QString& loaderUid)
{
    std::vector<QString> versions;
    if (!list) {
        return versions;
    }

    const bool anyMinecraft = loaderUid == "net.fabricmc.fabric-loader" || loaderUid == "org.quiltmc.quilt-loader";
    for (const auto& version : list->versions()) {
        if (!version) {
            continue;
        }
        if (anyMinecraft) {
            // Fabric and Quilt are not tied to a Minecraft version by the
            // launcher, but they do not support the versions before 1.14.
            if (Version(mcVersion) < Version("1.14")) {
                continue;
            }
        } else {
            const auto& requirements = version->requiredSet();
            auto it = std::ranges::find_if(requirements, [](const Meta::Require& requirement) {
                return requirement.uid == "net.minecraft";
            });
            if (it == requirements.end() || it->equalsVersion != mcVersion) {
                continue;
            }
        }
        versions.emplace_back(version->version());
    }
    return versions;
}

Meta::VersionList* loadVersionList(ModContext& ctx, const QString& uid, bool* networkFailed)
{
    auto* index = ctx.app.metadataIndex();
    auto list = index->get(uid);
    if (!list) {
        return nullptr;
    }
    if (!list->isLoaded()) {
        // The load task of the version list also loads the index itself, so
        // the list knows where its versions come from.
        if (auto task = list->getLoadTask(); task) {
            if (auto result = runTaskSync(task, g_MetadataTimeoutMs); !result) {
                qWarning() << "Could not load the version list of" << uid << ":" << result.error();
                if (networkFailed) {
                    *networkFailed = true;
                }
                return nullptr;
            }
        }
    }
    return list.get();
}

Result<QString, QString> resolveLoaderVersion(ModContext& ctx,
                                              const QString& loaderUid,
                                              const QString& wanted,
                                              const QString& mcVersion,
                                              bool& networkFailed)
{
    auto* list = loadVersionList(ctx, loaderUid, &networkFailed);
    if (!list) {
        return std::unexpected(QStringLiteral("the versions of the mod loader could not be loaded"));
    }

    const auto versions = versionsForMinecraft(list, mcVersion, loaderUid);
    if (versions.empty()) {
        return std::unexpected(QStringLiteral("the mod loader has no version for Minecraft %1").arg(mcVersion));
    }

    if (wanted.compare(QStringLiteral("latest"), Qt::CaseInsensitive) == 0) {
        // The list is sorted by the release date, the newest one is first.
        return versions.front();
    }

    for (const auto& version : versions) {
        if (version == wanted) {
            return version;
        }
    }
    return std::unexpected(QStringLiteral("the mod loader has no version '%1' for Minecraft %2").arg(wanted, mcVersion));
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
