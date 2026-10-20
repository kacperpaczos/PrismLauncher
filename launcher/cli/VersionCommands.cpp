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

#include "cli/CommandUtils.h"
#include "cli/VersionCommands.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "Application.h"
#include "BaseInstance.h"
#include "Result.h"
#include "cli/InstanceSupport.h"
#include "cli/TaskRunner.h"
#include "meta/Version.h"
#include "meta/VersionList.h"
#include "minecraft/Component.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/MinecraftLoadAndCheck.h"
#include "minecraft/PackProfile.h"
#include "net/Mode.h"
#include "tasks/SequentialTask.h"

namespace Cli {
namespace {

/// Upper bound for the resolution of the components of an instance.
constexpr int g_ResolveTimeoutMs = 600000;

/// The component uid of Minecraft.
constexpr auto g_MinecraftUid = "net.minecraft";

/**
 * Resolve the components of the profile of an instance and wait for it, so
 * that the versions of the components are known.
 */
ExitCode resolveProfile(PackProfile* profile)
{
    if (!profile) {
        return outputError(QStringLiteral("no_profile"),
                           QStringLiteral("The instance has no component profile"),
                           ExitCode::Generic);
    }
    profile->resolve(Net::Mode::Online);
    if (auto task = profile->getCurrentTask(); task) {
        if (auto result = runTaskSync(task, g_ResolveTimeoutMs); !result) {
            return outputError(QStringLiteral("network_error"),
                               QStringLiteral("The components of the instance could not be resolved: %1").arg(result.error()),
                               ExitCode::Network);
        }
    }
    return ExitCode::OK;
}

/// The Minecraft version of an instance.
QString minecraftVersion(PackProfile* profile)
{
    if (!profile) {
        return QString();
    }
    return componentVersion(profile->getComponent(QString::fromLatin1(g_MinecraftUid)));
}

/// The description of a component of an instance.
QJsonObject componentToJson(const shared_qobject_ptr<Component>& component)
{
    QJsonObject object;
    object.insert(QStringLiteral("uid"), component->getID());
    object.insert(QStringLiteral("name"), component->getName());
    object.insert(QStringLiteral("version"), componentVersion(component));
    object.insert(QStringLiteral("important"), component->m_important);
    object.insert(QStringLiteral("enabled"), component->isEnabled());
    object.insert(QStringLiteral("custom"), component->isCustom());
    object.insert(QStringLiteral("removable"), component->isRemovable());
    object.insert(QStringLiteral("version_changeable"), component->isVersionChangeable());
    return object;
}

/// The description of a version of a version list.
QJsonObject versionToJson(const Meta::Version::Ptr& version)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), version->version());
    object.insert(QStringLiteral("type"), version->typeString());
    const auto time = version->time();
    object.insert(QStringLiteral("date"), time.isValid() ? QJsonValue(time.toString(Qt::ISODate)) : QJsonValue(QJsonValue::Null));
    return object;
}

/// Whether a version of a version list has the wanted type.
bool matchesType(const Meta::Version::Ptr& version, const QString& type)
{
    if (type.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    return version->typeString().compare(type, Qt::CaseInsensitive) == 0;
}

}  // namespace

ExitCode runVersionCommand(ModContext& ctx, const Cmd::VersionShow& cmd)
{
    auto* instance = resolveInstance(ctx.app, cmd.id);
    if (!instance) {
        return outputError(QStringLiteral("instance_not_found"),
                           QStringLiteral("No instance with the id '%1' was found").arg(cmd.id),
                           ExitCode::NotFound);
    }

    loadProfile(instance);

    auto* profile = instance->getPackProfile();
    QJsonArray components;
    if (profile) {
        for (int i = 0; i < profile->rowCount(); ++i) {
            auto component = profile->getComponent(static_cast<size_t>(i));
            if (!component) {
                continue;
            }
            components.append(componentToJson(component));
        }
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.id);
    out.insert(QStringLiteral("mc_version"), minecraftVersion(profile));
    const auto loader = findLoader(profile);
    out.insert(QStringLiteral("loader"), loader ? QJsonValue(loader->name) : QJsonValue(QJsonValue::Null));
    out.insert(QStringLiteral("loader_version"), loader ? QJsonValue(loader->version) : QJsonValue(QJsonValue::Null));
    out.insert(QStringLiteral("total"), static_cast<int>(components.size()));
    out.insert(QStringLiteral("components"), components);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runVersionCommand(ModContext& ctx, const Cmd::VersionListAvailable& cmd)
{
    QJsonObject out;

    if (cmd.loader.has_value()) {
        const auto name = cmd.loader.value().toLower();
        const auto uid = loaderUids().value(name);
        if (uid.isEmpty()) {
            return outputError(QStringLiteral("invalid_loader"),
                               QStringLiteral("'%1' is not a known mod loader").arg(cmd.loader.value()),
                               ExitCode::Usage);
        }

        bool networkFailed = false;
        auto* list = loadVersionList(ctx, uid, &networkFailed);
        if (!list) {
            return outputError(networkFailed ? QStringLiteral("network_error") : QStringLiteral("no_versions"),
                               QStringLiteral("The versions of the mod loader could not be loaded"),
                               networkFailed ? ExitCode::Network : ExitCode::Generic);
        }

        QJsonArray versions;
        for (const auto& version : list->versions()) {
            if (!version || !matchesType(version, cmd.type)) {
                continue;
            }
            if (cmd.mcVersion.has_value()) {
                // Only the versions that support the given Minecraft version
                // are listed.
                const auto supported = versionsForMinecraft(list, cmd.mcVersion.value(), uid);
                if (std::ranges::find(supported, version->version()) == supported.end()) {
                    continue;
                }
            }
            versions.append(versionToJson(version));
        }

        out.insert(QStringLiteral("loader"), name);
        out.insert(QStringLiteral("uid"), uid);
        if (cmd.mcVersion.has_value()) {
            out.insert(QStringLiteral("mc_version"), cmd.mcVersion.value());
        }
        out.insert(QStringLiteral("type"), cmd.type);
        out.insert(QStringLiteral("total"), static_cast<int>(versions.size()));
        out.insert(QStringLiteral("versions"), versions);
        outputJson(QJsonDocument(out));
        return ExitCode::OK;
    }

    // The versions of Minecraft.
    bool networkFailed = false;
    auto* list = loadVersionList(ctx, QString::fromLatin1(g_MinecraftUid), &networkFailed);
    if (!list) {
        return outputError(networkFailed ? QStringLiteral("network_error") : QStringLiteral("no_versions"),
                           QStringLiteral("The versions of Minecraft could not be loaded"),
                           networkFailed ? ExitCode::Network : ExitCode::Generic);
    }

    QJsonArray versions;
    for (const auto& version : list->versions()) {
        if (!version || !matchesType(version, cmd.type)) {
            continue;
        }
        versions.append(versionToJson(version));
    }

    out.insert(QStringLiteral("uid"), QString::fromLatin1(g_MinecraftUid));
    out.insert(QStringLiteral("type"), cmd.type);
    out.insert(QStringLiteral("total"), static_cast<int>(versions.size()));
    out.insert(QStringLiteral("versions"), versions);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runVersionCommand(ModContext& ctx, const Cmd::VersionChangeMc& cmd)
{
    auto* instance = resolveInstance(ctx.app, cmd.id);
    if (!instance) {
        return outputError(QStringLiteral("instance_not_found"),
                           QStringLiteral("No instance with the id '%1' was found").arg(cmd.id),
                           ExitCode::NotFound);
    }
    if (auto code = checkInstanceRunning(instance, cmd.id, QStringLiteral("change the version of the instance")); code != ExitCode::OK) {
        return code;
    }

    // The version has to exist, otherwise the instance would be broken.
    bool networkFailed = false;
    auto* mcList = loadVersionList(ctx, QString::fromLatin1(g_MinecraftUid), &networkFailed);
    if (!mcList) {
        return outputError(networkFailed ? QStringLiteral("network_error") : QStringLiteral("no_versions"),
                           QStringLiteral("The versions of Minecraft could not be loaded"),
                           networkFailed ? ExitCode::Network : ExitCode::Generic);
    }
    if (!mcList->hasVersion(cmd.version)) {
        return outputError(QStringLiteral("version_not_found"),
                           QStringLiteral("There is no Minecraft version '%1'").arg(cmd.version),
                           ExitCode::NotFound);
    }

    loadProfile(instance);
    auto* profile = instance->getPackProfile();
    if (!profile) {
        return outputError(QStringLiteral("no_profile"),
                           QStringLiteral("The instance has no component profile"),
                           ExitCode::Generic);
    }

    // The Java version of the instance is not known anymore when the
    // Minecraft version changes, so the instance falls back to the automatic
    // Java of the launcher - the same way the version page of the GUI does
    // it.
    auto* settings = instance->settings();
    if (ctx.app.settings()->get(QStringLiteral("AutomaticJavaSwitch")).toBool() &&
        settings->get(QStringLiteral("AutomaticJava")).toBool() &&
        settings->get(QStringLiteral("OverrideJavaLocation")).toBool()) {
        settings->set(QStringLiteral("OverrideJavaLocation"), false);
        settings->set(QStringLiteral("JavaPath"), QString());
    }
    if (settings->get(QStringLiteral("UseLatestMinecraftVersion")).toBool()) {
        settings->set(QStringLiteral("UseLatestMinecraftVersion"), false);
    }

    if (!profile->setComponentVersion(QString::fromLatin1(g_MinecraftUid), cmd.version, /*important=*/true)) {
        return outputError(QStringLiteral("version_change_failed"),
                           QStringLiteral("The Minecraft version of the instance could not be changed"),
                           ExitCode::Conflict);
    }

    if (auto code = resolveProfile(profile); code != ExitCode::OK) {
        return code;
    }
    profile->saveNow();

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.id);
    out.insert(QStringLiteral("mc_version"), componentVersion(profile->getComponent(QString::fromLatin1(g_MinecraftUid))));
    out.insert(QStringLiteral("changed"), cmd.version);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runVersionCommand(ModContext& ctx, const Cmd::VersionInstallLoader& cmd)
{
    auto* instance = resolveInstance(ctx.app, cmd.id);
    if (!instance) {
        return outputError(QStringLiteral("instance_not_found"),
                           QStringLiteral("No instance with the id '%1' was found").arg(cmd.id),
                           ExitCode::NotFound);
    }
    if (auto code = checkInstanceRunning(instance, cmd.id, QStringLiteral("install a mod loader into the instance"));
        code != ExitCode::OK) {
        return code;
    }

    const auto name = cmd.loader.toLower();
    const auto uid = loaderUids().value(name);
    if (uid.isEmpty()) {
        return outputError(QStringLiteral("invalid_loader"),
                           QStringLiteral("'%1' is not a known mod loader").arg(cmd.loader),
                           ExitCode::Usage);
    }

    loadProfile(instance);
    auto* profile = instance->getPackProfile();
    if (!profile) {
        return outputError(QStringLiteral("no_profile"),
                           QStringLiteral("The instance has no component profile"),
                           ExitCode::Generic);
    }

    const auto mcVersion = minecraftVersion(profile);
    if (mcVersion.isEmpty()) {
        return outputError(QStringLiteral("no_minecraft_version"),
                           QStringLiteral("The instance has no Minecraft version"),
                           ExitCode::Conflict);
    }

    bool networkFailed = false;
    auto resolved = resolveLoaderVersion(ctx, uid, cmd.version, mcVersion, networkFailed);
    if (!resolved) {
        return outputError(networkFailed ? QStringLiteral("network_error") : QStringLiteral("version_not_found"),
                           resolved.error(),
                           networkFailed ? ExitCode::Network : ExitCode::NotFound);
    }
    const auto loaderVersion = resolved.value();

    // The loaders that cannot be used together with the new one are handled
    // the way the install loader dialog of the GUI does it.
    QJsonArray conflicts;
    const auto known = Component::KNOWN_MODLOADERS.constFind(uid);
    if (known != Component::KNOWN_MODLOADERS.constEnd()) {
        for (const auto& conflictUid : known->knownConflictingComponents) {
            const auto conflict = profile->getComponent(conflictUid);
            if (!conflict || !conflict->isEnabled() || conflict->isCustom()) {
                continue;
            }
            QJsonObject entry;
            entry.insert(QStringLiteral("uid"), conflictUid);
            entry.insert(QStringLiteral("name"), conflict->getName());
            entry.insert(QStringLiteral("version"), componentVersion(conflict));

            if (cmd.forceConflict == QLatin1String("keep")) {
                entry.insert(QStringLiteral("action"), QStringLiteral("kept"));
            } else if (cmd.forceConflict == QLatin1String("replace")) {
                if (!profile->remove(conflictUid)) {
                    entry.insert(QStringLiteral("action"), QStringLiteral("kept"));
                    conflicts.append(entry);
                    continue;
                }
                entry.insert(QStringLiteral("action"), QStringLiteral("removed"));
            } else {
                conflict->setEnabled(false);
                entry.insert(QStringLiteral("action"), QStringLiteral("disabled"));
            }
            conflicts.append(entry);
        }
    }

    if (!profile->setComponentVersion(uid, loaderVersion)) {
        return outputError(QStringLiteral("loader_install_failed"),
                           QStringLiteral("The mod loader could not be installed into the instance"),
                           ExitCode::Conflict);
    }
    // The mod loader is enabled, even when it was disabled before.
    if (const auto component = profile->getComponent(uid); component) {
        component->setEnabled(true);
    }

    if (auto code = resolveProfile(profile); code != ExitCode::OK) {
        return code;
    }
    profile->saveNow();

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.id);
    out.insert(QStringLiteral("loader"), name);
    out.insert(QStringLiteral("uid"), uid);
    out.insert(QStringLiteral("loader_version"), componentVersion(profile->getComponent(uid)));
    out.insert(QStringLiteral("mc_version"), minecraftVersion(profile));
    out.insert(QStringLiteral("conflicts"), conflicts);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runVersionCommand(ModContext& ctx, const Cmd::VersionRemoveLoader& cmd)
{
    auto* instance = resolveInstance(ctx.app, cmd.id);
    if (!instance) {
        return outputError(QStringLiteral("instance_not_found"),
                           QStringLiteral("No instance with the id '%1' was found").arg(cmd.id),
                           ExitCode::NotFound);
    }
    if (auto code = checkInstanceRunning(instance, cmd.id, QStringLiteral("remove the mod loader of the instance"));
        code != ExitCode::OK) {
        return code;
    }

    const auto name = cmd.loader.toLower();
    const auto uid = loaderUids().value(name);
    if (uid.isEmpty()) {
        return outputError(QStringLiteral("invalid_loader"),
                           QStringLiteral("'%1' is not a known mod loader").arg(cmd.loader),
                           ExitCode::Usage);
    }

    loadProfile(instance);
    auto* profile = instance->getPackProfile();
    if (!profile) {
        return outputError(QStringLiteral("no_profile"),
                           QStringLiteral("The instance has no component profile"),
                           ExitCode::Generic);
    }

    const auto component = profile->getComponent(uid);
    if (!component) {
        return outputError(QStringLiteral("loader_not_found"),
                           QStringLiteral("The instance does not use the mod loader '%1'").arg(cmd.loader),
                           ExitCode::NotFound);
    }
    if (!component->isRemovable()) {
        // The component is important, so the launcher does not let it be
        // removed.
        return outputError(QStringLiteral("not_removable"),
                           QStringLiteral("The mod loader '%1' cannot be removed from the instance").arg(cmd.loader),
                           ExitCode::Ambiguous);
    }

    if (!profile->remove(uid)) {
        return outputError(QStringLiteral("loader_remove_failed"),
                           QStringLiteral("The mod loader could not be removed from the instance"),
                           ExitCode::Conflict);
    }

    if (auto code = resolveProfile(profile); code != ExitCode::OK) {
        return code;
    }
    profile->saveNow();

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.id);
    out.insert(QStringLiteral("loader"), name);
    out.insert(QStringLiteral("uid"), uid);
    out.insert(QStringLiteral("removed"), true);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runVersionCommand(ModContext& ctx, const Cmd::VersionReinstall& cmd)
{
    auto* instance = resolveInstance(ctx.app, cmd.id);
    if (!instance) {
        return outputError(QStringLiteral("instance_not_found"),
                           QStringLiteral("No instance with the id '%1' was found").arg(cmd.id),
                           ExitCode::NotFound);
    }
    if (auto code = checkInstanceRunning(instance, cmd.id, QStringLiteral("reinstall the instance")); code != ExitCode::OK) {
        return code;
    }

    auto* profile = instance->getPackProfile();
    if (!profile) {
        return outputError(QStringLiteral("no_profile"),
                           QStringLiteral("The instance has no component profile"),
                           ExitCode::Generic);
    }

    // The components of the instance are loaded again, so the metadata of the
    // components is fetched from the network.
    if (auto result = profile->reload(Net::Mode::Online); !result) {
        return outputError(QStringLiteral("network_error"),
                           QStringLiteral("The components of the instance could not be loaded: %1").arg(result.error()),
                           ExitCode::Network);
    }
    if (auto task = profile->getCurrentTask(); task) {
        if (auto result = runTaskSync(task, g_ResolveTimeoutMs); !result) {
            return outputError(QStringLiteral("network_error"),
                               QStringLiteral("The components of the instance could not be resolved: %1").arg(result.error()),
                               ExitCode::Network);
        }
    }

    // The files of the instance are downloaded again.
    auto task = makeShared<SequentialTask>(QStringLiteral("Reinstall the files of the instance"));
    task->addTask(makeShared<MinecraftLoadAndCheck>(instance, Net::Mode::Online));
    for (const auto& updateTask : instance->createUpdateTask()) {
        task->addTask(updateTask);
    }
    if (auto result = runTaskSync(task, g_ResolveTimeoutMs); !result) {
        return outputError(QStringLiteral("download_failed"),
                           QStringLiteral("The files of the instance could not be downloaded: %1").arg(result.error()),
                           ExitCode::Network);
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.id);
    out.insert(QStringLiteral("mc_version"), minecraftVersion(profile));
    out.insert(QStringLiteral("reinstalled"), true);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace Cli
