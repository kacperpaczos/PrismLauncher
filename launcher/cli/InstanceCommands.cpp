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

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>

#include "Application.h"
#include "BaseInstance.h"
#include "FileSystem.h"
#include "InstanceCopyPrefs.h"
#include "InstanceCopyTask.h"
#include "InstanceImportTask.h"
#include "InstanceList.h"
#include "InstanceTask.h"
#include "cli/InstanceSupport.h"
#include "cli/TaskRunner.h"
#include "icons/IconList.h"
#include "icons/IconUtils.h"
#include "meta/Index.h"
#include "meta/Version.h"
#include "meta/VersionList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/MinecraftLoadAndCheck.h"
#include "minecraft/PackProfile.h"
#include "net/HttpMetaCache.h"
#include "minecraft/VanillaInstanceCreationTask.h"
#include "net/Mode.h"
#include "tasks/SequentialTask.h"

namespace Cli {
namespace {

/// Upper bound for the download of the files of an instance.
constexpr int g_DownloadTimeoutMs = 900000;

/**
 * The icon key of a new instance. A path of an image file is installed into
 * the icon directory of the launcher, everything else is used as it is.
 */
QString resolveIconKey(ModContext& ctx, const QString& icon, QString* installedFrom)
{
    auto* icons = ctx.app.icons();
    if (!icons) {
        return icon;
    }

    QFileInfo info(icon);
    if (!info.exists() || !info.isFile()) {
        return icon;
    }
    if (!IconUtils::isIconSuffix(info.suffix())) {
        return icon;
    }

    const auto key = info.completeBaseName();
    const auto target = key + "." + info.suffix();
    const auto targetPath = FS::PathCombine(icons->getDirectory(), target);
    // `QFile::copy` refuses to overwrite, so the old icon is removed first.
    QFile::remove(targetPath);
    icons->installIcon(icon, target);
    if (installedFrom) {
        *installedFrom = targetPath;
    }
    return key;
}

/**
 * Check that the files of an instance are there.
 *
 * The tasks of the launcher can finish without doing anything when the
 * metadata of the components could not be loaded, so the result is checked
 * here: the components of the instance have to be resolved and the jar of
 * the Minecraft version has to be downloaded.
 */
ExitCode verifyInstanceFiles(ModContext& ctx, MinecraftInstance* instance)
{
    auto* profile = instance->getPackProfile();
    if (!profile) {
        return outputError(QStringLiteral("verification_failed"),
                           QStringLiteral("The instance has no component profile"),
                           ExitCode::Conflict);
    }

    const auto mcVersion = profile->getComponentVersion(QStringLiteral("net.minecraft"));
    if (mcVersion.isEmpty()) {
        return outputError(QStringLiteral("verification_failed"),
                           QStringLiteral("The components of the instance could not be resolved"),
                           ExitCode::Conflict);
    }

    auto launch = profile->getProfile();
    if (!launch || launch->getMinecraftVersion().isEmpty()) {
        return outputError(QStringLiteral("verification_failed"),
                           QStringLiteral("The launch profile of the instance could not be built"),
                           ExitCode::Conflict);
    }

    // The jar of the Minecraft version is downloaded into the shared library
    // directory of the launcher, so the path of the file is the one of the
    // cache of the launcher.
    auto mainJar = launch->getMainJar();
    if (mainJar) {
        const auto storage = mainJar->rawName().toPath(mainJar->filename(instance->runtimeContext()));
        auto* cache = ctx.app.metacache();
        if (!cache) {
            return outputError(QStringLiteral("verification_failed"),
                               QStringLiteral("The launcher has no cache for the files of the libraries"),
                               ExitCode::Conflict);
        }
        const auto entry = cache->resolveEntry(QStringLiteral("libraries"), storage);
        if (!entry || !QFileInfo::exists(entry->getFullPath())) {
            return outputError(QStringLiteral("verification_failed"),
                               QStringLiteral("The files of the Minecraft version '%1' were not downloaded").arg(mcVersion),
                               ExitCode::Conflict);
        }
    }

    return ExitCode::OK;
}

/**
 * Download the files of an instance and check that the instance loads.
 */
ExitCode downloadInstanceFiles(ModContext& ctx, MinecraftInstance* instance, ExitCode failureCode)
{
    auto task = makeShared<SequentialTask>(QStringLiteral("Download the files of the instance"));
    task->addTask(makeShared<MinecraftLoadAndCheck>(instance, Net::Mode::Online));
    for (const auto& updateTask : instance->createUpdateTask()) {
        task->addTask(updateTask);
    }

    if (auto result = runTaskSync(task, g_DownloadTimeoutMs); !result) {
        QJsonObject out;
        out.insert(QStringLiteral("reason"), result.error());
        return outputError(QStringLiteral("download_failed"),
                           QStringLiteral("The files of the instance could not be downloaded: %1").arg(result.error()),
                           failureCode,
                           out);
    }
    // The download of the creation task of the launcher cannot report its
    // failures, so the result is verified here: the components of the
    // instance have to be resolved and the files of the instance have to be
    // there.
    if (auto code = verifyInstanceFiles(ctx, instance); code != ExitCode::OK) {
        return code;
    }
    return ExitCode::OK;
}

/**
 * The instance that a creation task committed. The id of a new instance is
 * the (sanitized) name of the instance, so it can be computed upfront.
 */
QString expectedInstanceId(ModContext& ctx, const QString& name)
{
    return FS::DirNameFromString(name, ctx.app.instances()->instanceDirs());
}

}  // namespace

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
            entry.insert(QStringLiteral("version"), componentVersion(component));
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

ExitCode runInstanceCommand(ModContext& ctx, const Cmd::InstanceCreate& cmd)
{
    auto* instances = ctx.app.instances();
    if (!instances) {
        return outputError(QStringLiteral("no_instances"), QStringLiteral("The launcher has no instance list"), ExitCode::Generic);
    }

    // The icon of a new instance is either an icon key or the path of an
    // image file that is installed into the icon directory.
    QString installedIcon;
    const auto iconKey = cmd.icon.has_value() ? resolveIconKey(ctx, cmd.icon.value(), &installedIcon) : QString();

    if (cmd.from.has_value()) {
        // ---- import an instance from a URL or a file ----
        const auto source = cmd.from.value();
        QFileInfo info(source);
        const auto url = info.exists() && info.isFile() ? QUrl::fromLocalFile(info.absoluteFilePath()) : QUrl(source);
        if (!url.isValid()) {
            return outputError(QStringLiteral("invalid_source"),
                               QStringLiteral("'%1' is not a valid URL or file").arg(source),
                               ExitCode::Usage);
        }

        auto* task = new InstanceImportTask(url, /*trustedSource=*/true, /*parent=*/nullptr);
        task->setName(cmd.name);
        task->setGroup(cmd.group.value_or(QString()));
        task->setIcon(iconKey.isEmpty() ? QStringLiteral("default") : iconKey);
        task->setTargetDir(instances->primaryDir());
        // A headless run cannot answer the question whether an existing
        // instance should be updated, so the question is never asked.
        task->setConfirmUpdate(false);

        const auto expectedId = expectedInstanceId(ctx, cmd.name);
        shared_qobject_ptr<Task> wrapped(instances->wrapInstanceTask(task));
        if (auto result = runTaskSync(wrapped, g_DownloadTimeoutMs); !result) {
            return outputError(QStringLiteral("create_failed"),
                               QStringLiteral("The instance could not be imported: %1").arg(result.error()),
                               ExitCode::Conflict);
        }

        instances->loadList();
        auto* instance = instances->getInstanceById(expectedId);
        if (!instance) {
            return outputError(QStringLiteral("create_failed"),
                               QStringLiteral("The instance was imported, but it could not be found afterwards"),
                               ExitCode::Generic);
        }

        QJsonObject out;
        out.insert(QStringLiteral("id"), instance->id());
        out.insert(QStringLiteral("name"), instance->name());
        out.insert(QStringLiteral("path"), instance->instanceRoot());
        out.insert(QStringLiteral("imported_from"), source);
        if (!installedIcon.isEmpty()) {
            out.insert(QStringLiteral("icon_file"), installedIcon);
        }
        outputJson(QJsonDocument(out));
        return ExitCode::OK;
    }

    // ---- create a vanilla instance ----
    bool networkFailed = false;
    auto* mcList = loadVersionList(ctx, "net.minecraft", &networkFailed);
    if (!mcList) {
        return outputError(networkFailed ? QStringLiteral("network_error") : QStringLiteral("no_versions"),
                           QStringLiteral("The versions of Minecraft could not be loaded"),
                           networkFailed ? ExitCode::Network : ExitCode::Generic);
    }
    if (!mcList->hasVersion(cmd.mcVersion)) {
        return outputError(QStringLiteral("version_not_found"),
                           QStringLiteral("There is no Minecraft version '%1'").arg(cmd.mcVersion),
                           ExitCode::NotFound);
    }

    auto mcVersion = ctx.app.metadataIndex()->getLoadedVersion("net.minecraft", cmd.mcVersion);
    if (!mcVersion) {
        return outputError(QStringLiteral("version_not_found"),
                           QStringLiteral("The metadata of the Minecraft version '%1' could not be loaded").arg(cmd.mcVersion),
                           ExitCode::Network);
    }

    QString loaderUid;
    BaseVersion::Ptr loaderVersion;
    if (cmd.loader.has_value() && cmd.loader.value().compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0) {
        const auto name = cmd.loader.value().toLower();
        loaderUid = loaderUids().value(name);
        if (loaderUid.isEmpty()) {
            return outputError(QStringLiteral("invalid_loader"),
                               QStringLiteral("'%1' is not a known mod loader").arg(cmd.loader.value()),
                               ExitCode::Usage);
        }

        const auto wanted = cmd.loaderVersion.value_or(QStringLiteral("latest"));
        auto resolved = resolveLoaderVersion(ctx, loaderUid, wanted, cmd.mcVersion, networkFailed);
        if (!resolved) {
            return outputError(networkFailed ? QStringLiteral("network_error") : QStringLiteral("version_not_found"),
                               resolved.error(),
                               networkFailed ? ExitCode::Network : ExitCode::NotFound);
        }
        loaderVersion = ctx.app.metadataIndex()->getLoadedVersion(loaderUid, resolved.value());
        if (!loaderVersion) {
            return outputError(QStringLiteral("version_not_found"),
                               QStringLiteral("The metadata of the mod loader version '%1' could not be loaded").arg(resolved.value()),
                               ExitCode::Network);
        }
    }

    // The files of the instance are downloaded by the command itself - the
    // download of the creation task cannot report its failures (it shows a
    // dialog, which a headless run cannot answer).
    auto* settings = ctx.app.settings();
    const auto downloadDuringCreation = settings->get("DownloadGameFilesDuringInstanceCreation");
    settings->set("DownloadGameFilesDuringInstanceCreation", false);

    auto* task = loaderUid.isEmpty() ? new VanillaCreationTask(mcVersion) : new VanillaCreationTask(mcVersion, loaderUid, loaderVersion);
    task->setName(cmd.name);
    task->setGroup(cmd.group.value_or(QString()));
    task->setIcon(iconKey.isEmpty() ? QStringLiteral("default") : iconKey);
    task->setTargetDir(instances->primaryDir());

    const auto expectedId = expectedInstanceId(ctx, cmd.name);
    shared_qobject_ptr<Task> wrapped(instances->wrapInstanceTask(task));
    auto result = runTaskSync(wrapped, g_DownloadTimeoutMs);

    settings->set("DownloadGameFilesDuringInstanceCreation", downloadDuringCreation);

    if (!result) {
        return outputError(QStringLiteral("create_failed"),
                           QStringLiteral("The instance could not be created: %1").arg(result.error()),
                           ExitCode::Conflict);
    }

    instances->loadList();
    auto* instance = instances->getInstanceById(expectedId);
    if (!instance) {
        return outputError(QStringLiteral("create_failed"),
                           QStringLiteral("The instance was created, but it could not be found afterwards"),
                           ExitCode::Generic);
    }

    if (cmd.download) {
        if (auto code = downloadInstanceFiles(ctx, instance, ExitCode::Conflict); code != ExitCode::OK) {
            return code;
        }
    }

    QJsonObject out;
    out.insert(QStringLiteral("id"), instance->id());
    out.insert(QStringLiteral("name"), instance->name());
    out.insert(QStringLiteral("path"), instance->instanceRoot());
    out.insert(QStringLiteral("mc_version"), cmd.mcVersion);
    if (!loaderUid.isEmpty()) {
        out.insert(QStringLiteral("loader"), cmd.loader.value());
        out.insert(QStringLiteral("loader_version"), loaderVersion->descriptor());
    }
    out.insert(QStringLiteral("downloaded"), cmd.download);
    if (!installedIcon.isEmpty()) {
        out.insert(QStringLiteral("icon_file"), installedIcon);
    }
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runInstanceCommand(ModContext& ctx, const Cmd::InstanceDuplicate& cmd)
{
    auto* instance = resolveInstance(ctx, cmd.id);
    if (!instance) {
        return outputError(QStringLiteral("instance_not_found"),
                           QStringLiteral("No instance with the id '%1' was found").arg(cmd.id),
                           ExitCode::NotFound);
    }
    if (auto code = checkInstanceRunning(instance, cmd.id, QStringLiteral("duplicate the instance")); code != ExitCode::OK) {
        return code;
    }

    auto* instances = ctx.app.instances();

    InstanceCopyPrefs prefs;
    if (cmd.copyMode == QStringLiteral("links")) {
        prefs.enableUseSymLinks(true);
        prefs.enableLinkRecursively(true);
    } else if (cmd.copyMode == QStringLiteral("hardlinks")) {
        prefs.enableUseSymLinks(true);
        prefs.enableLinkRecursively(true);
        prefs.enableUseHardLinks(true);
    } else if (cmd.copyMode == QStringLiteral("clone")) {
        prefs.enableUseClone(true);
    }
    if (cmd.noSaves) {
        prefs.enableCopySaves(false);
    }
    if (cmd.noMods) {
        prefs.enableCopyMods(false);
    }
    if (cmd.noResourcePacks) {
        prefs.enableCopyResourcePacks(false);
    }
    if (cmd.noShaderPacks) {
        prefs.enableCopyShaderPacks(false);
    }
    if (cmd.noOptions) {
        prefs.enableCopyGameOptions(false);
    }

    const auto name = cmd.name.value_or(instance->name() + QStringLiteral(" (copy)"));

    auto* task = new InstanceCopyTask(instance, prefs);
    task->setName(name);
    task->setGroup(instances->getInstanceGroup(cmd.id));
    task->setIcon(instance->iconKey());
    task->setTargetDir(instances->primaryDir());

    const auto expectedId = expectedInstanceId(ctx, name);
    shared_qobject_ptr<Task> wrapped(instances->wrapInstanceTask(task));
    if (auto result = runTaskSync(wrapped, g_DownloadTimeoutMs); !result) {
        return outputError(QStringLiteral("duplicate_failed"),
                           QStringLiteral("The instance could not be duplicated: %1").arg(result.error()),
                           ExitCode::Conflict);
    }

    instances->loadList();
    auto* copy = instances->getInstanceById(expectedId);
    if (!copy) {
        return outputError(QStringLiteral("duplicate_failed"),
                           QStringLiteral("The copy was created, but it could not be found afterwards"),
                           ExitCode::Generic);
    }

    if (cmd.noConfigs) {
        // The copy preferences of the launcher have no switch for the
        // configs, so the folder is removed from the copy afterwards.
        FS::deletePath(FS::PathCombine(copy->gameRoot(), QStringLiteral("config")));
    }

    QJsonObject out;
    out.insert(QStringLiteral("id"), copy->id());
    out.insert(QStringLiteral("name"), copy->name());
    out.insert(QStringLiteral("path"), copy->instanceRoot());
    out.insert(QStringLiteral("source"), cmd.id);
    out.insert(QStringLiteral("copy_mode"), cmd.copyMode);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runInstanceCommand(ModContext& ctx, const Cmd::InstanceRename& cmd)
{
    auto* instance = resolveInstance(ctx, cmd.id);
    if (!instance) {
        return outputError(QStringLiteral("instance_not_found"),
                           QStringLiteral("No instance with the id '%1' was found").arg(cmd.id),
                           ExitCode::NotFound);
    }
    if (auto code = checkInstanceRunning(instance, cmd.id, QStringLiteral("rename the instance")); code != ExitCode::OK) {
        return code;
    }

    auto* instances = ctx.app.instances();
    const auto oldId = instance->id();
    const auto oldName = instance->name();

    if (!instance->getLinkedInstances().isEmpty()) {
        // The GUI asks the user whether the linked instances should be
        // renamed too - a headless run cannot ask, so the rename is refused.
        return outputError(QStringLiteral("linked_instances"),
                           QStringLiteral("The instance '%1' has linked instances, refusing to rename it").arg(cmd.id),
                           ExitCode::Conflict);
    }

    instance->setName(cmd.newName);

    auto newId = oldId;
    if (cmd.renameFolder) {
        const auto newDirName = FS::DirNameFromString(cmd.newName, instances->instanceDirs());
        const auto newRoot = FS::PathCombine(QFileInfo(instance->instanceRoot()).dir().absolutePath(), newDirName);
        if (newRoot != instance->instanceRoot()) {
            if (QDir(newRoot).exists()) {
                return outputError(QStringLiteral("instance_exists"),
                                   QStringLiteral("The folder '%1' already exists").arg(newDirName),
                                   ExitCode::Conflict);
            }
            if (!instance->syncInstanceDirName(newRoot)) {
                return outputError(QStringLiteral("rename_failed"),
                                   QStringLiteral("The folder of the instance could not be renamed to '%1'").arg(newDirName),
                                   ExitCode::Conflict);
            }
            newId = QFileInfo(newRoot).fileName();

            // The group of the instance belongs to its id, so it has to
            // follow the instance to its new folder.
            const auto group = instances->getInstanceGroup(oldId);
            if (!group.isEmpty() && oldId != newId) {
                instances->setInstanceGroup(oldId, GroupId());
                instances->loadList();
                instances->setInstanceGroup(newId, group);
            }
        }
    }

    instances->loadList();

    QJsonObject out;
    out.insert(QStringLiteral("old_id"), oldId);
    out.insert(QStringLiteral("new_id"), newId);
    out.insert(QStringLiteral("old_name"), oldName);
    out.insert(QStringLiteral("name"), cmd.newName);
    out.insert(QStringLiteral("folder_renamed"), newId != oldId);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runInstanceCommand(ModContext& ctx, const Cmd::InstanceDelete& cmd)
{
    auto* instance = resolveInstance(ctx, cmd.id);
    if (!instance) {
        return outputError(QStringLiteral("instance_not_found"),
                           QStringLiteral("No instance with the id '%1' was found").arg(cmd.id),
                           ExitCode::NotFound);
    }
    if (auto code = checkInstanceRunning(instance, cmd.id, QStringLiteral("delete the instance")); code != ExitCode::OK) {
        return code;
    }

    auto* instances = ctx.app.instances();
    const auto path = instance->instanceRoot();

    // The instance is moved to the trash by default. The trash is not
    // available everywhere (a headless machine has no desktop), so the
    // files are deleted permanently when the trash cannot be used - the same
    // fallback the GUI takes.
    bool trashed = false;
    if (!cmd.purge) {
        trashed = instances->trashInstance(cmd.id);
    }
    if (!trashed) {
        instances->deleteInstance(cmd.id);
    }

    instances->loadList();

    QJsonObject out;
    out.insert(QStringLiteral("id"), cmd.id);
    out.insert(QStringLiteral("path"), path);
    out.insert(QStringLiteral("method"), trashed ? QStringLiteral("trash") : QStringLiteral("delete"));
    out.insert(QStringLiteral("purged"), cmd.purge);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runInstanceCommand(ModContext& ctx, const Cmd::InstanceIconSet& cmd)
{
    auto* instance = resolveInstance(ctx, cmd.id);
    if (!instance) {
        return outputError(QStringLiteral("instance_not_found"),
                           QStringLiteral("No instance with the id '%1' was found").arg(cmd.id),
                           ExitCode::NotFound);
    }
    if (auto code = checkInstanceRunning(instance, cmd.id, QStringLiteral("change the icon of the instance")); code != ExitCode::OK) {
        return code;
    }

    auto* icons = ctx.app.icons();
    if (!icons) {
        return outputError(QStringLiteral("no_icons"), QStringLiteral("The launcher has no icon list"), ExitCode::Generic);
    }

    QString installedIcon;
    const auto key = resolveIconKey(ctx, cmd.icon, &installedIcon);
    if (installedIcon.isEmpty()) {
        // The icon is not a file, so it has to be an existing icon key.
        if (!icons->icon(key)) {
            return outputError(QStringLiteral("icon_not_found"),
                               QStringLiteral("There is no icon with the key '%1'").arg(cmd.icon),
                               ExitCode::NotFound);
        }
    }

    instance->setIconKey(key);

    QJsonObject out;
    out.insert(QStringLiteral("id"), cmd.id);
    out.insert(QStringLiteral("icon_key"), key);
    if (!installedIcon.isEmpty()) {
        out.insert(QStringLiteral("icon_file"), installedIcon);
    }
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace Cli
