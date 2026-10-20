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

#include "cli/ModCommands.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QUrl>

#include "Application.h"
#include "InstanceList.h"
#include "ResourceDownloadTask.h"
#include "Version.h"
#include "cli/CommandUtils.h"
#include "cli/TaskRunner.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/MetadataHandler.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourceFolderModel.h"
#include "minecraft/mod/tasks/GetModDependenciesTask.h"
#include "modplatform/CheckUpdateTask.h"
#include "modplatform/EnsureMetadataTask.h"
#include "modplatform/ModIndex.h"
#include "modplatform/ResourceAPI.h"
#include "modplatform/ResourceType.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "modplatform/modrinth/ModrinthCheckUpdate.h"
#include "modplatform/modrinth/ModrinthPackIndex.h"
#include "net/ApiRequest.h"
#include "net/NetJob.h"
#include "tasks/ConcurrentTask.h"
#include "tasks/SequentialTask.h"

namespace Cli {
namespace {

// The Modrinth search API always uses a page size of 25, so the results have
// to be requested page by page.
constexpr int g_SearchPageSize = 25;
// Upper bound for a single network request.
constexpr int g_NetworkTimeoutMs = 180000;

/**
 * Resolve the instance of a mod command. The error is reported and the exit
 * code is returned when the instance does not exist.
 */
ExitCode resolveModInstance(ModContext& ctx, const QString& instanceId, MinecraftInstance*& instance)
{
    instance = resolveInstance(ctx.app, instanceId);
    if (!instance) {
        return outputError(QStringLiteral("instance_not_found"),
                           QStringLiteral("No instance with the id '%1' was found").arg(instanceId),
                           ExitCode::NotFound);
    }
    return ExitCode::OK;
}

/**
 * Open the mod model of an instance and wait for it to be up to date. The
 * error is reported and the exit code is returned when it fails.
 */
ExitCode openModModel(MinecraftInstance* instance, const QString& instanceId, ModFolderModel*& model)
{
    model = instance->loaderModList();
    if (!model) {
        return outputError(QStringLiteral("no_mod_list"), QStringLiteral("The instance has no mod list"), ExitCode::Generic);
    }
    if (!waitForModel(model)) {
        return outputError(QStringLiteral("model_timeout"),
                           QStringLiteral("Timed out while reading the mods of the instance '%1'").arg(instanceId),
                           ExitCode::Generic);
    }
    return ExitCode::OK;
}

/**
 * A mod that is being (or has been) downloaded.
 */
struct DownloadEntry {
    QString slug;
    QString fileName;
    QString projectId;
    QString versionId;
    QString versionNumber;
    QString releaseType;
    QString source;
    bool isDependency = false;
};

/**
 * The JSON of a downloaded mod. It is used when the mod is not in the model
 * or when it has no metadata (because it was installed with `--no-metadata`).
 */
QJsonObject downloadedModToJson(const DownloadEntry& entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), entry.slug);
    object.insert(QStringLiteral("slug"), entry.slug);
    object.insert(QStringLiteral("project_id"), entry.projectId);
    object.insert(QStringLiteral("version_id"), entry.versionId);
    object.insert(QStringLiteral("version_number"), entry.versionNumber);
    object.insert(QStringLiteral("file"), entry.fileName);
    object.insert(QStringLiteral("enabled"), true);
    object.insert(QStringLiteral("date"), QJsonValue(QJsonValue::Null));
    QJsonObject hash;
    hash.insert(QStringLiteral("sha1"), QJsonValue(QJsonValue::Null));
    hash.insert(QStringLiteral("sha512"), QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("hash"), hash);
    object.insert(QStringLiteral("source"), entry.source);
    object.insert(QStringLiteral("release_type"), entry.releaseType);
    object.insert(QStringLiteral("lock_update"), false);
    return object;
}

/**
 * Find a resource by the name of the file it is stored in.
 */
Resource* findResourceByFileName(ResourceFolderModel* model, const QString& fileName)
{
    if (!model || fileName.isEmpty()) {
        return nullptr;
    }
    for (auto* resource : model->allResources()) {
        if (resource->fileinfo().fileName() == fileName) {
            return resource;
        }
    }
    return nullptr;
}

ModPlatform::ModLoaderTypes loadersFromStrings(const std::vector<QString>& loaders, QString* invalid)
{
    ModPlatform::ModLoaderTypes types;
    for (const auto& loader : loaders) {
        auto type = ModPlatform::getModLoaderFromString(loader.trimmed().toLower());
        if (type == ModPlatform::ModLoaderType::None) {
            if (invalid) {
                *invalid = loader;
            }
            return types;
        }
        types |= type;
    }
    return types;
}

bool versionSupportsLoader(const ModPlatform::IndexedVersion& version, const QList<ModPlatform::ModLoaderType>& loaders)
{
    if (version.loaders == ModPlatform::ModLoaderTypes(0)) {
        // The provider did not tell us anything about the loader.
        return true;
    }
    for (const auto loader : loaders) {
        if (version.loaders.testAnyFlags(loader)) {
            return true;
        }
    }
    return false;
}

/**
 * Collect the mods that depend on the given ones (or that are required by
 * them). The relation graph of the model is used, the same way the GUI does
 * it for the "disable dependants" prompt. The result maps every affected mod
 * to the mods that caused it to be affected.
 */
QHash<Mod*, QStringList> collectAffectedMods(ModFolderModel* model, const QList<Mod*>& selected, EnableAction action)
{
    QHash<Mod*, QStringList> affected;
    QList<Mod*> queue = selected;
    QSet<QString> seen;

    for (auto* mod : selected) {
        seen.insert(mod->internalId());
    }

    while (!queue.isEmpty()) {
        auto* parent = queue.takeFirst();
        QStringList relations;
        if (action == EnableAction::ENABLE) {
            relations = model->requiresList(parent->modId());
        } else {
            relations = model->requiredByList(parent->modId());
        }
        for (const auto& relation : relations) {
            for (auto* candidate : model->allMods()) {
                if (candidate->name() != relation || seen.contains(candidate->internalId())) {
                    continue;
                }
                seen.insert(candidate->internalId());
                affected.insert(candidate, QStringList{ parent->name() });
                queue.append(candidate);
            }
        }
    }

    return affected;
}

/**
 * Shared implementation of `mod enable` and `mod disable`.
 */
ExitCode runEnableDisable(ModContext& ctx,
                          const std::vector<QString>& targets,
                          const QString& instanceId,
                          bool recursive,
                          EnableAction action)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveModInstance(ctx, instanceId, instance); code != ExitCode::OK) {
        return code;
    }
    if (auto code = checkInstanceRunning(instance, instanceId, QStringLiteral("change mods")); code != ExitCode::OK) {
        return code;
    }

    ModFolderModel* model = nullptr;
    if (auto code = openModModel(instance, instanceId, model); code != ExitCode::OK) {
        return code;
    }

    auto resolved = resolveTargets(model, targets, QStringLiteral("mod"));
    if (resolved.ambiguous) {
        QJsonObject out;
        out.insert(QStringLiteral("candidates"), resolved.candidates);
        out.insert(QStringLiteral("failed"), resolved.failed);
        return outputError(QStringLiteral("ambiguous_target"),
                           QStringLiteral("Some targets matched more than one mod"),
                           ExitCode::Ambiguous,
                           out);
    }
    if (resolved.resources.isEmpty()) {
        QJsonObject out;
        out.insert(QStringLiteral("changed"), QJsonArray());
        out.insert(QStringLiteral("failed"), resolved.failed);
        return outputError(QStringLiteral("not_found"),
                           QStringLiteral("None of the targets matched a mod"),
                           ExitCode::NotFound,
                           out);
    }

    // The recursive mode also enables the required mods (or disables the
    // mods that depend on the selected ones).
    QList<Mod*> selectedMods;
    QHash<const Resource*, QStringList> affectedBy;
    if (recursive) {
        for (auto* resource : resolved.resources) {
            if (auto* mod = dynamic_cast<Mod*>(resource)) {
                selectedMods.append(mod);
            }
        }
        const auto affectedMods = collectAffectedMods(model, selectedMods, action);
        for (auto it = affectedMods.constBegin(); it != affectedMods.constEnd(); ++it) {
            auto* resource = static_cast<Resource*>(it.key());
            if (!resolved.resources.contains(resource)) {
                resolved.resources.append(resource);
            }
            affectedBy.insert(resource, it.value());
        }
    }

    QJsonArray changed;
    QJsonArray failed = resolved.failed;
    for (auto* resource : resolved.resources) {
        const bool wantEnabled = action == EnableAction::ENABLE;
        if (resource->enabled() == wantEnabled) {
            // The mod is already in the wanted state - nothing to do.
            continue;
        }
        if (!resource->enable(action)) {
            QJsonObject entry;
            entry.insert(QStringLiteral("target"), resource->fileinfo().fileName());
            entry.insert(QStringLiteral("reason"),
                         QStringLiteral("the mod could not be %1").arg(wantEnabled ? "enabled" : "disabled"));
            failed.append(entry);
            continue;
        }
        QJsonObject entry = resourceToJson(resource, true);
        QJsonArray affected;
        for (const auto& cause : affectedBy.value(resource)) {
            affected.append(cause);
        }
        entry.insert(QStringLiteral("affected_by"), affected);
        changed.append(entry);
    }

    QJsonObject out;
    out.insert(QStringLiteral("changed"), changed);
    out.insert(QStringLiteral("failed"), failed);
    if (changed.isEmpty() && !failed.isEmpty()) {
        return outputError(QStringLiteral("change_failed"),
                           QStringLiteral("None of the mods could be changed"),
                           ExitCode::Generic,
                           out);
    }
    if (!resolved.failed.isEmpty()) {
        // Some of the targets did not match a mod - the mods that were found
        // are changed anyway, but the command reports the failure.
        return outputError(QStringLiteral("not_found"),
                           QStringLiteral("Some of the targets did not match a mod"),
                           ExitCode::NotFound,
                           out);
    }
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace

ExitCode runModCommand(ModContext& ctx, const Cmd::ModList& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveModInstance(ctx, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }

    ModFolderModel* model = nullptr;
    if (auto code = openModModel(instance, cmd.instance, model); code != ExitCode::OK) {
        return code;
    }

    QJsonArray mods;
    for (auto* resource : model->allResources()) {
        if (!cmd.includeDisabled && !resource->enabled()) {
            continue;
        }
        mods.append(resourceToJson(resource, true));
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("total"), static_cast<int>(mods.size()));
    out.insert(QStringLiteral("mods"), mods);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runModCommand(ModContext& ctx, const Cmd::ModSearch& cmd)
{
    ResourceAPI::SearchArgs args;
    args.type = ModPlatform::ResourceType::Mod;
    args.offset = cmd.offset;
    if (!cmd.query.isEmpty()) {
        args.search = cmd.query;
    }
    if (!cmd.mcVersions.empty()) {
        std::vector<Version> versions;
        versions.reserve(cmd.mcVersions.size());
        for (const auto& version : cmd.mcVersions) {
            versions.emplace_back(version);
        }
        args.versions = versions;
    }
    if (!cmd.loaders.empty()) {
        QString invalid;
        auto loaders = loadersFromStrings(cmd.loaders, &invalid);
        if (!invalid.isEmpty()) {
            return outputError(QStringLiteral("invalid_loader"),
                               QStringLiteral("'%1' is not a known mod loader").arg(invalid),
                               ExitCode::Usage);
        }
        args.loaders = loaders;
    }

    QJsonArray results;
    int total = 0;
    int offset = cmd.offset;

    while (results.size() < cmd.limit) {
        args.offset = offset;

        // The search endpoint hardcodes the page size, so the results have to
        // be requested page by page. The raw response is used (instead of the
        // RPC sink of `searchProjectsTask`) because it also carries the total
        // number of hits and the download count of every project.
        auto spec = ModrinthAPI::get().searchProjects(args);
        auto [action, response] = Net::ApiRequest::makeByteArray(spec.first.url, Net::Request::Option::AutoRetry);
        auto job = makeShared<NetJob>(QStringLiteral("Modrinth::Search"), APPLICATION->network());
        job->addNetAction(action);

        if (auto result = runTaskSync(job, g_NetworkTimeoutMs); !result) {
            return outputError(QStringLiteral("network_error"),
                               QStringLiteral("The Modrinth search failed: %1").arg(result.error()),
                               ExitCode::Network);
        }

        QJsonParseError parseError;
        auto document = QJsonDocument::fromJson(*response, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return outputError(QStringLiteral("network_error"),
                               QStringLiteral("The Modrinth search returned an invalid response: %1").arg(parseError.errorString()),
                               ExitCode::Network);
        }

        auto object = document.object();
        total = object.value(QStringLiteral("total_hits")).toInt(total);
        auto hits = object.value(QStringLiteral("hits")).toArray();

        for (const auto& hit : hits) {
            if (results.size() >= cmd.limit) {
                break;
            }
            auto hitObject = hit.toObject();
            ModPlatform::IndexedPack pack;
            if (auto parsed = Modrinth::Parse::loadIndexedPack(pack, hitObject); !parsed) {
                qWarning() << "Could not parse a Modrinth search hit:" << parsed.error();
                continue;
            }

            QJsonObject entry;
            entry.insert(QStringLiteral("slug"), pack.slug);
            entry.insert(QStringLiteral("project_id"), pack.addonId.toString());
            entry.insert(QStringLiteral("name"), pack.name);
            entry.insert(QStringLiteral("description"), pack.description);
            QJsonArray authors;
            for (const auto& author : pack.authors) {
                authors.append(author.name);
            }
            entry.insert(QStringLiteral("authors"), authors);
            entry.insert(QStringLiteral("website"), pack.websiteUrl);
            entry.insert(QStringLiteral("icon_url"), pack.logoUrl);
            entry.insert(QStringLiteral("downloads"), static_cast<int>(hitObject.value(QStringLiteral("downloads")).toInt(0)));
            results.append(entry);
        }

        if (hits.size() < g_SearchPageSize) {
            // The last page was reached.
            break;
        }
        offset += g_SearchPageSize;
    }

    QJsonObject out;
    out.insert(QStringLiteral("offset"), cmd.offset);
    out.insert(QStringLiteral("limit"), cmd.limit);
    out.insert(QStringLiteral("total"), total);
    out.insert(QStringLiteral("results"), results);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runModCommand(ModContext& ctx, const Cmd::ModInfo& cmd)
{
    auto project = fetchProject(cmd.project);
    if (!project) {
        if (project.error().contains(QStringLiteral("was not found"))) {
            return outputError(QStringLiteral("project_not_found"), project.error(), ExitCode::NotFound);
        }
        return outputError(QStringLiteral("network_error"), project.error(), ExitCode::Network);
    }
    const auto pack = project.value();
    auto packPtr = std::make_shared<ModPlatform::IndexedPack>(pack);

    auto versionsResult = fetchProjectVersions(packPtr, ModPlatform::ResourceType::Mod);
    if (!versionsResult) {
        return outputError(QStringLiteral("network_error"), versionsResult.error(), ExitCode::Network);
    }

    QJsonArray versions;
    for (const auto& version : versionsResult.value()) {
        QJsonObject entry;
        entry.insert(QStringLiteral("version_id"), version.fileId.toString());
        entry.insert(QStringLiteral("version_number"), version.versionNumber);
        entry.insert(QStringLiteral("name"), version.version);
        entry.insert(QStringLiteral("release_type"),
                     version.versionType.isValid() ? version.versionType.toString() : QStringLiteral("Unknown"));
        entry.insert(QStringLiteral("date"), jsonNullIfEmpty(version.date));
        entry.insert(QStringLiteral("file_name"), version.fileName);
        QJsonArray mcVersions;
        for (const auto& mcVersion : version.mcVersion) {
            mcVersions.append(mcVersion);
        }
        entry.insert(QStringLiteral("mc_versions"), mcVersions);
        QJsonArray loaders;
        for (const auto loader : ModPlatform::modLoaderTypesToList(version.loaders)) {
            loaders.append(ModPlatform::getModLoaderAsString(loader));
        }
        entry.insert(QStringLiteral("loaders"), loaders);
        versions.append(entry);
    }

    QJsonObject projectObject;
    projectObject.insert(QStringLiteral("slug"), pack.slug);
    projectObject.insert(QStringLiteral("project_id"), pack.addonId.toString());
    projectObject.insert(QStringLiteral("name"), pack.name);
    projectObject.insert(QStringLiteral("description"), pack.description);
    QJsonArray authors;
    for (const auto& author : pack.authors) {
        authors.append(author.name);
    }
    projectObject.insert(QStringLiteral("authors"), authors);
    projectObject.insert(QStringLiteral("website"), pack.websiteUrl);
    projectObject.insert(QStringLiteral("icon_url"), pack.logoUrl);
    projectObject.insert(QStringLiteral("body"), pack.extraData.body);
    projectObject.insert(QStringLiteral("status"), pack.extraData.status);
    projectObject.insert(QStringLiteral("source_url"), pack.extraData.sourceUrl);
    projectObject.insert(QStringLiteral("issues_url"), pack.extraData.issuesUrl);
    projectObject.insert(QStringLiteral("wiki_url"), pack.extraData.wikiUrl);
    projectObject.insert(QStringLiteral("discord_url"), pack.extraData.discordUrl);

    // The installed mod is looked up by slug or project id.
    QJsonValue installed = QJsonValue(QJsonValue::Null);
    if (cmd.instance.has_value()) {
        MinecraftInstance* instance = nullptr;
        if (auto code = resolveModInstance(ctx, cmd.instance.value(), instance); code != ExitCode::OK) {
            return code;
        }
        ModFolderModel* model = nullptr;
        if (openModModel(instance, cmd.instance.value(), model) == ExitCode::OK) {
            for (auto* resource : model->allResources()) {
                auto meta = resource->metadata();
                if (!meta) {
                    continue;
                }
                if (normalizedSlug(meta->slug) == pack.slug ||
                    (meta->projectId.isValid() && meta->projectId.toString() == pack.addonId.toString())) {
                    installed = resourceToJson(resource, true);
                    break;
                }
            }
        }
    }

    QJsonObject out;
    out.insert(QStringLiteral("project"), projectObject);
    out.insert(QStringLiteral("versions"), versions);
    out.insert(QStringLiteral("installed"), installed);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runModCommand(ModContext& ctx, const Cmd::ModInstall& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveModInstance(ctx, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }
    if (auto code = checkInstanceRunning(instance, cmd.instance, QStringLiteral("install mods")); code != ExitCode::OK) {
        return code;
    }

    ModFolderModel* model = nullptr;
    if (auto code = openModModel(instance, cmd.instance, model); code != ExitCode::OK) {
        return code;
    }

    if (!loadComponentProfile(instance)) {
        return outputError(QStringLiteral("no_profile"),
                           QStringLiteral("The component profile of the instance could not be loaded"),
                           ExitCode::Generic);
    }
    auto* profile = instance->getPackProfile();
    if (!profile) {
        return outputError(QStringLiteral("no_profile"), QStringLiteral("The instance has no component profile"), ExitCode::Generic);
    }

    const QString mcVersionString = profile->getComponentVersion(QStringLiteral("net.minecraft"));
    if (mcVersionString.isEmpty()) {
        return outputError(QStringLiteral("no_minecraft_version"),
                           QStringLiteral("The instance '%1' has no Minecraft component").arg(cmd.instance),
                           ExitCode::Generic);
    }
    const auto loaders = profile->getModLoadersList();
    if (loaders.isEmpty()) {
        return outputError(QStringLiteral("no_mod_loader"),
                           QStringLiteral("The instance '%1' has no mod loader").arg(cmd.instance),
                           ExitCode::Generic);
    }

    auto project = fetchProject(cmd.project);
    if (!project) {
        if (project.error().contains(QStringLiteral("was not found"))) {
            return outputError(QStringLiteral("project_not_found"), project.error(), ExitCode::NotFound);
        }
        return outputError(QStringLiteral("network_error"), project.error(), ExitCode::Network);
    }
    auto packPtr = std::make_shared<ModPlatform::IndexedPack>(project.value());

    auto versionsResult = fetchProjectVersions(packPtr, ModPlatform::ResourceType::Mod);
    if (!versionsResult) {
        return outputError(QStringLiteral("network_error"), versionsResult.error(), ExitCode::Network);
    }
    const auto allVersions = versionsResult.value();

    std::optional<ModPlatform::IndexedVersion> selected;
    if (cmd.version.has_value()) {
        const auto& wanted = cmd.version.value();
        for (const auto& version : allVersions) {
            if (version.fileId.toString() == wanted || version.versionNumber == wanted || version.version == wanted) {
                selected = version;
                break;
            }
        }
        if (!selected.has_value()) {
            return outputError(QStringLiteral("version_not_found"),
                               QStringLiteral("No version '%1' was found for the project '%2'").arg(wanted, cmd.project),
                               ExitCode::NotFound);
        }
    } else {
        for (const auto& version : allVersions) {
            if (!version.mcVersion.contains(mcVersionString)) {
                continue;
            }
            if (!versionSupportsLoader(version, loaders)) {
                continue;
            }
            selected = version;
            break;
        }
        if (!selected.has_value()) {
            return outputError(QStringLiteral("version_not_found"),
                               QStringLiteral("No version of '%1' was found for Minecraft %2").arg(cmd.project, mcVersionString),
                               ExitCode::NotFound);
        }
    }

    // Resolve the dependencies of the selected version.
    QList<std::shared_ptr<GetModDependenciesTask::PackDependency>> dependencies;
    if (!cmd.noDeps) {
        QList<std::shared_ptr<GetModDependenciesTask::PackDependency>> selectedDeps;
        selectedDeps.append(std::make_shared<GetModDependenciesTask::PackDependency>(packPtr, selected.value()));
        auto dependencyTask = makeShared<GetModDependenciesTask>(instance, model, selectedDeps);
        if (auto result = runTaskSync(dependencyTask, g_NetworkTimeoutMs); !result) {
            return outputError(QStringLiteral("network_error"),
                               QStringLiteral("Could not resolve the dependencies: %1").arg(result.error()),
                               ExitCode::Network);
        }
        dependencies = dependencyTask->getDependecies();
    }

    std::vector<DownloadEntry> entries;

    // The failure list is shared with the download task, which may outlive
    // this function (the tasks are deleted through the event loop).
    auto failed = std::make_shared<QJsonArray>();
    auto download = makeShared<ConcurrentTask>(
        QStringLiteral("Download Mods"), qMax(1, APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt()));

    auto addDownload = [&](const ModPlatform::IndexedPack::Ptr& pack,
                           const ModPlatform::IndexedVersion& version,
                           bool isDependency,
                           const QString& dependentOn = {}) {
        auto task =
            makeShared<ResourceDownloadTask>(pack, version, model, !cmd.noMetadata, isDependency ? "dependency" : "install", dependentOn);
        QObject::connect(task.get(), &Task::failed, download.get(), [pack, failed](const QString& reason) {
            QJsonObject entry;
            entry.insert(QStringLiteral("slug"), pack->slug);
            entry.insert(QStringLiteral("reason"), reason);
            failed->append(entry);
        });
        download->addTask(task);
        entries.push_back({ .slug = pack->slug,
                            .fileName = version.fileName,
                            .projectId = pack->addonId.toString(),
                            .versionId = version.fileId.toString(),
                            .versionNumber = version.versionNumber,
                            .releaseType = version.versionType.isValid() ? version.versionType.toString() : QString(),
                            .source = sourceName(pack->provider),
                            .isDependency = isDependency });
    };

    addDownload(packPtr, selected.value(), false);
    for (const auto& dependency : dependencies) {
        if (!dependency || !dependency->pack) {
            continue;
        }
        // The dependency is required by the mod that was selected, so the
        // version of that mod is reported to the API.
        addDownload(dependency->pack, dependency->version, true, selected.value().fileId.toString());
    }

    if (auto result = runTaskSync(download, g_NetworkTimeoutMs); !result) {
        QJsonObject out;
        out.insert(QStringLiteral("failed"), *failed);
        out.insert(QStringLiteral("reason"), result.error());
        return outputError(QStringLiteral("download_failed"), result.error(), ExitCode::Network, out);
    }

    // Refresh the model so the freshly installed mods show up in the output.
    waitForModel(model);

    QJsonArray installed;
    QJsonArray dependencyObjects;
    for (const auto& entry : entries) {
        auto* resource = findResourceByFileName(model, entry.fileName);
        QJsonObject object = resource ? resourceToJson(resource, true) : QJsonObject();
        if (object.isEmpty()) {
            // The mod was not picked up by the model, so the data of the
            // download is used instead.
            object = downloadedModToJson(entry);
        } else if (!resource->metadata()) {
            // The mod was installed without metadata, so the metadata of the
            // download is reported instead of the missing one.
            const auto downloaded = downloadedModToJson(entry);
            for (auto it = downloaded.constBegin(); it != downloaded.constEnd(); ++it) {
                if (it.key() == QLatin1String("name") || it.key() == QLatin1String("file") ||
                    it.key() == QLatin1String("enabled") || it.key() == QLatin1String("date") ||
                    it.key() == QLatin1String("hash") || it.key() == QLatin1String("lock_update")) {
                    continue;
                }
                object.insert(it.key(), it.value());
            }
        }
        if (entry.isDependency) {
            dependencyObjects.append(object);
        } else {
            installed.append(object);
        }
    }

    QJsonObject out;
    out.insert(QStringLiteral("installed"), installed);
    out.insert(QStringLiteral("dependencies"), dependencyObjects);
    out.insert(QStringLiteral("failed"), *failed);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runModCommand(ModContext& ctx, const Cmd::ModRemove& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveModInstance(ctx, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }
    if (!cmd.force) {
        if (auto code = checkInstanceRunning(instance, cmd.instance, QStringLiteral("remove mods"), QStringLiteral("use --force"));
            code != ExitCode::OK) {
            return code;
        }
    }

    ModFolderModel* model = nullptr;
    if (auto code = openModModel(instance, cmd.instance, model); code != ExitCode::OK) {
        return code;
    }

    auto resolved = resolveTargets(model, cmd.targets, QStringLiteral("mod"), cmd.force);
    if (resolved.ambiguous) {
        QJsonObject out;
        out.insert(QStringLiteral("candidates"), resolved.candidates);
        out.insert(QStringLiteral("failed"), resolved.failed);
        return outputError(QStringLiteral("ambiguous_target"),
                           QStringLiteral("Some targets matched more than one mod"),
                           ExitCode::Ambiguous,
                           out);
    }

    QJsonArray removed;
    QJsonArray failed = resolved.failed;
    bool lockedRefused = false;
    const auto indexDir = model->indexDir();
    // The model is not updated in between the removals - the resolved list is
    // iterated instead.
    for (auto* resource : resolved.resources) {
        auto fileName = resource->fileinfo().fileName();
        auto name = resource->name();
        if (resource->lockUpdate() && !cmd.force) {
            QJsonObject entry;
            entry.insert(QStringLiteral("target"), fileName);
            entry.insert(QStringLiteral("reason"), QStringLiteral("the mod is locked to a specific version (use --force)"));
            failed.append(entry);
            lockedRefused = true;
            continue;
        }
        // The metadata is gone once the mod is destroyed, so it has to be
        // read before.
        auto meta = resource->metadata();
        auto slug = meta ? normalizedSlug(meta->slug) : QString();
        if (!resource->destroy(indexDir, false, true)) {
            QJsonObject entry;
            entry.insert(QStringLiteral("target"), fileName);
            entry.insert(QStringLiteral("reason"), QStringLiteral("the mod could not be removed"));
            failed.append(entry);
            continue;
        }
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), name);
        entry.insert(QStringLiteral("file"), fileName);
        entry.insert(QStringLiteral("slug"), jsonNullIfEmpty(slug));
        removed.append(entry);
    }

    QJsonObject out;
    out.insert(QStringLiteral("removed"), removed);
    out.insert(QStringLiteral("failed"), failed);
    if (removed.isEmpty() && !failed.isEmpty()) {
        if (resolved.resources.isEmpty()) {
            return outputError(QStringLiteral("not_found"),
                               QStringLiteral("None of the targets matched a mod"),
                               ExitCode::NotFound,
                               out);
        }
        if (lockedRefused) {
            return outputError(QStringLiteral("locked"),
                               QStringLiteral("The mod is locked to a specific version (use --force to remove it anyway)"),
                               ExitCode::Conflict,
                               out);
        }
        return outputError(QStringLiteral("remove_failed"),
                           QStringLiteral("None of the mods could be removed"),
                           ExitCode::Generic,
                           out);
    }
    if (!resolved.failed.isEmpty()) {
        // Some of the targets did not match a mod - the mods that were found
        // are removed anyway, but the command reports the failure.
        return outputError(QStringLiteral("not_found"),
                           QStringLiteral("Some of the targets did not match a mod"),
                           ExitCode::NotFound,
                           out);
    }
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runModCommand(ModContext& ctx, const Cmd::ModEnable& cmd)
{
    return runEnableDisable(ctx, cmd.targets, cmd.instance, cmd.recursive, EnableAction::ENABLE);
}

ExitCode runModCommand(ModContext& ctx, const Cmd::ModDisable& cmd)
{
    return runEnableDisable(ctx, cmd.targets, cmd.instance, cmd.recursive, EnableAction::DISABLE);
}

ExitCode runModCommand(ModContext& ctx, const Cmd::ModUpdate& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveModInstance(ctx, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }
    if (!cmd.force) {
        if (auto code = checkInstanceRunning(instance, cmd.instance, QStringLiteral("update mods"), QStringLiteral("use --force"));
            code != ExitCode::OK) {
            return code;
        }
    }

    ModFolderModel* model = nullptr;
    if (auto code = openModModel(instance, cmd.instance, model); code != ExitCode::OK) {
        return code;
    }

    if (!loadComponentProfile(instance)) {
        return outputError(QStringLiteral("no_profile"),
                           QStringLiteral("The component profile of the instance could not be loaded"),
                           ExitCode::Generic);
    }
    auto* profile = instance->getPackProfile();
    if (!profile) {
        return outputError(QStringLiteral("no_profile"), QStringLiteral("The instance has no component profile"), ExitCode::Generic);
    }
    const QString mcVersionString = profile->getComponentVersion(QStringLiteral("net.minecraft"));
    if (mcVersionString.isEmpty()) {
        return outputError(QStringLiteral("no_minecraft_version"),
                           QStringLiteral("The instance '%1' has no Minecraft component").arg(cmd.instance),
                           ExitCode::Generic);
    }

    // The context of the connections that are made below. It is declared
    // after the variables that the lambdas use, so that it is destroyed first
    // and the connections cannot outlive them.
    QList<Resource*> failedChecks;
    QObject connectionContext;

    QList<Resource*> candidates;
    QJsonArray failed;
    bool hasUnresolvedTargets = false;
    if (cmd.all) {
        candidates = model->allResources();
    } else {
        auto resolved = resolveTargets(model, cmd.targets, QStringLiteral("mod"));
        if (resolved.ambiguous) {
            QJsonObject out;
            out.insert(QStringLiteral("candidates"), resolved.candidates);
            out.insert(QStringLiteral("failed"), resolved.failed);
            return outputError(QStringLiteral("ambiguous_target"),
                               QStringLiteral("Some targets matched more than one mod"),
                               ExitCode::Ambiguous,
                               out);
        }
        if (resolved.resources.isEmpty()) {
            QJsonObject out;
            out.insert(QStringLiteral("failed"), resolved.failed);
            return outputError(QStringLiteral("not_found"),
                               QStringLiteral("None of the targets matched a mod"),
                               ExitCode::NotFound,
                               out);
        }
        candidates = resolved.resources;
        // The targets that did not match any mod are reported as failed.
        failed = resolved.failed;
        hasUnresolvedTargets = !resolved.failed.isEmpty();
    }

    QJsonArray skipped;
    QList<Resource*> updatable;

    for (auto* resource : candidates) {
        if (resource->lockUpdate() && !cmd.force) {
            QJsonObject entry;
            entry.insert(QStringLiteral("name"), resource->name());
            entry.insert(QStringLiteral("file"), resource->fileinfo().fileName());
            entry.insert(QStringLiteral("reason"), QStringLiteral("the mod is locked to a specific version"));
            skipped.append(entry);
            continue;
        }
        if (resource->type() == ResourceType::FOLDER) {
            QJsonObject entry;
            entry.insert(QStringLiteral("name"), resource->name());
            entry.insert(QStringLiteral("file"), resource->fileinfo().fileName());
            entry.insert(QStringLiteral("reason"), QStringLiteral("folders cannot be updated"));
            skipped.append(entry);
            continue;
        }
        updatable.append(resource);
    }

    // Make sure every mod has metadata, otherwise it cannot be checked for
    // updates. The provider is taken from the managed pack, defaulting to
    // Modrinth (there is no interactive provider chooser in a headless run).
    QList<Resource*> withoutMetadata;
    for (auto* resource : updatable) {
        if (!resource->metadata()) {
            withoutMetadata.append(resource);
        }
    }
    if (!withoutMetadata.empty()) {
        // The managed pack type is only used to pick the provider - there is
        // no interactive provider chooser in a headless run, so Modrinth is
        // used for every mod.
        auto provider = ModPlatform::ResourceProvider::MODRINTH;
        auto metadataTask = makeShared<EnsureMetadataTask>(withoutMetadata, model->indexDir(), provider);
        auto sequence = makeShared<SequentialTask>(QStringLiteral("Looking for metadata"));
        if (auto hashingTask = metadataTask->getHashingTask()) {
            sequence->addTask(hashingTask);
        }
        sequence->addTask(metadataTask);
        if (auto result = runTaskSync(sequence, g_NetworkTimeoutMs); !result) {
            QJsonObject out;
            out.insert(QStringLiteral("failed"), failed);
            out.insert(QStringLiteral("reason"), result.error());
            return outputError(QStringLiteral("metadata_failed"),
                               QStringLiteral("Could not generate the mod metadata: %1").arg(result.error()),
                               ExitCode::Network,
                               out);
        }
    }

    // Mods without metadata cannot be checked for updates.
    QList<Resource*> checkable;
    for (auto* resource : updatable) {
        if (!resource->metadata()) {
            QJsonObject entry;
            entry.insert(QStringLiteral("name"), resource->name());
            entry.insert(QStringLiteral("file"), resource->fileinfo().fileName());
            entry.insert(QStringLiteral("reason"), QStringLiteral("no metadata is available for the mod"));
            failed.append(entry);
            continue;
        }
        checkable.append(resource);
    }

    QJsonArray updated;
    QJsonArray upToDate;
    QJsonArray dependencies;
    QList<shared_qobject_ptr<ResourceDownloadTask>> updateDownloads;

    if (!checkable.isEmpty()) {
        std::vector<Version> mcVersions{ Version(mcVersionString) };
        auto loaders = profile->getModLoadersList();

        auto checkTask = makeShared<ModrinthCheckUpdate>(checkable, mcVersions, loaders, model);

        // The mods for which no version was found are reported as failed -
        // they are not up to date, they could not be checked at all.
        QObject::connect(checkTask.get(),
                         &CheckUpdateTask::checkFailed,
                         &connectionContext,
                         [&failedChecks, &failed](Resource* resource, const QString& reason, const QUrl&) {
                             if (!resource) {
                                 return;
                             }
                             failedChecks.append(resource);
                             QJsonObject entry;
                             entry.insert(QStringLiteral("name"), resource->name());
                             entry.insert(QStringLiteral("file"), resource->fileinfo().fileName());
                             entry.insert(QStringLiteral("reason"), reason);
                             failed.append(entry);
                         });

        if (auto result = runTaskSync(checkTask, g_NetworkTimeoutMs); !result) {
            QJsonObject out;
            out.insert(QStringLiteral("failed"), failed);
            out.insert(QStringLiteral("reason"), result.error());
            return outputError(QStringLiteral("check_failed"),
                               QStringLiteral("Could not check for updates: %1").arg(result.error()),
                               ExitCode::Network,
                               out);
        }

        auto updates = checkTask->getUpdates();
        for (const auto& update : updates) {
            QJsonObject entry;
            entry.insert(QStringLiteral("name"), update.name);
            entry.insert(QStringLiteral("old_version"), update.oldVersion);
            entry.insert(QStringLiteral("new_version"), update.newVersion);
            entry.insert(QStringLiteral("release_type"),
                         update.newVersionType.has_value() ? update.newVersionType.value().toString() : QString());
            entry.insert(QStringLiteral("provider"), sourceName(update.provider));
            if (update.download) {
                entry.insert(QStringLiteral("slug"), normalizedSlug(update.download->getPack()->slug));
                entry.insert(QStringLiteral("file"), update.download->getFilename());
                updateDownloads.append(update.download);
            }
            updated.append(entry);
        }

        // The list that the check task returns holds the updated mods
        // themselves - it is the input of the dependency resolution. The
        // dependencies are resolved (and reported), but they are not
        // downloaded: the update only replaces the mods that have a newer
        // version.
        if (!cmd.noDeps && !checkTask->getDependencies().isEmpty()) {
            auto dependencyTask = makeShared<GetModDependenciesTask>(instance, model, checkTask->getDependencies());
            if (auto result = runTaskSync(dependencyTask, g_NetworkTimeoutMs); !result) {
                QJsonObject entry;
                entry.insert(QStringLiteral("reason"), QStringLiteral("the dependencies could not be resolved: %1").arg(result.error()));
                failed.append(entry);
            } else {
                for (const auto& dependency : dependencyTask->getDependecies()) {
                    if (!dependency || !dependency->pack) {
                        continue;
                    }
                    QJsonObject entry;
                    entry.insert(QStringLiteral("slug"), normalizedSlug(dependency->pack->slug));
                    entry.insert(QStringLiteral("project_id"), dependency->pack->addonId.toString());
                    entry.insert(QStringLiteral("version_id"), dependency->version.fileId.toString());
                    entry.insert(QStringLiteral("version_number"), dependency->version.versionNumber);
                    entry.insert(QStringLiteral("file"), dependency->version.fileName);
                    dependencies.append(entry);
                }
            }
        }

        // Everything that was checked but has no update available.
        for (auto* resource : checkable) {
            if (failedChecks.contains(resource)) {
                // The check failed for this mod - it is already reported.
                continue;
            }
            auto meta = resource->metadata();
            auto hasUpdate = std::any_of(updates.begin(), updates.end(), [meta](const auto& update) {
                return update.download && update.download->getPack()->addonId.toString() == meta->projectId.toString();
            });
            if (!hasUpdate) {
                QJsonObject entry;
                entry.insert(QStringLiteral("name"), resource->name());
                entry.insert(QStringLiteral("file"), resource->fileinfo().fileName());
                entry.insert(QStringLiteral("version"), meta->versionNumber);
                upToDate.append(entry);
            }
        }
    }

    if (!cmd.dryRun && !updateDownloads.isEmpty()) {
        auto download = makeShared<ConcurrentTask>(
            QStringLiteral("Update Mods"), qMax(1, APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt()));
        for (const auto& update : updateDownloads) {
            download->addTask(update);
        }
        if (auto result = runTaskSync(download, g_NetworkTimeoutMs); !result) {
            QJsonObject out;
            out.insert(QStringLiteral("updated"), updated);
            out.insert(QStringLiteral("failed"), failed);
            out.insert(QStringLiteral("reason"), result.error());
            return outputError(QStringLiteral("download_failed"),
                               QStringLiteral("Could not download the updates: %1").arg(result.error()),
                               ExitCode::Network,
                               out);
        }
        waitForModel(model);
    }

    QJsonObject out;
    out.insert(QStringLiteral("dry_run"), cmd.dryRun);
    out.insert(QStringLiteral("updated"), updated);
    out.insert(QStringLiteral("up_to_date"), upToDate);
    out.insert(QStringLiteral("skipped"), skipped);
    out.insert(QStringLiteral("failed"), failed);
    out.insert(QStringLiteral("dependencies"), dependencies);
    if (hasUnresolvedTargets) {
        // Some of the targets did not match a mod - the mods that were found
        // are updated anyway, but the command reports the failure.
        return outputError(QStringLiteral("not_found"),
                           QStringLiteral("Some of the targets did not match a mod"),
                           ExitCode::NotFound,
                           out);
    }
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace Cli
