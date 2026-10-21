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
#include "BuildConfig.h"
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
#include "net/Mode.h"
#include "net/NetJob.h"
#include "tasks/ConcurrentTask.h"
#include "tasks/SequentialTask.h"

namespace Cli {
namespace {

// The Modrinth search API always uses a page size of 25, so the results have
// to be requested page by page.
// Upper bound for a single network request.
constexpr int g_NetworkTimeoutMs = 180000;

/**
 * Resolve the instance of a mod command. The error is reported and the exit
 * code is returned when the instance does not exist.
 */
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

/**
 * A net job for the headless commands. The jobs of the GUI ask the user
 * whether a failed request should be retried - a dialog cannot be answered
 * in a headless run, so the jobs of the CLI never ask.
 */
/**
 * The members of the team of a project. The project of the API does not carry
 * the author, it only carries the id of the team, so the members have to be
 * fetched separately.
 */
Result<QList<ModPlatform::ModpackAuthor>> fetchProjectAuthors(const QString& projectId)
{
    const auto url = QUrl(BuildConfig.MODRINTH_PROD_URL + "/project/" + projectId + "/members");
    auto [action, response] = Net::ApiRequest::makeByteArray(url);
    auto job = makeCliNetJob(QStringLiteral("Modrinth::ProjectMembers"));
    job->addNetAction(action);

    if (auto result = runTaskSync(job, g_NetworkTimeoutMs); !result) {
        return std::unexpected(result.error());
    }

    QJsonParseError parseError;
    auto document = QJsonDocument::fromJson(*response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        return std::unexpected(QStringLiteral("the Modrinth team response is not a JSON array"));
    }

    QList<ModPlatform::ModpackAuthor> authors;
    for (const auto& member : document.array()) {
        auto user = member.toObject().value(QStringLiteral("user")).toObject();
        auto name = user.value(QStringLiteral("username")).toString();
        if (name.isEmpty()) {
            continue;
        }
        authors.append({ .name = name, .url = QStringLiteral("https://modrinth.com/user/") + name });
    }
    return authors;
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
ExitCode runEnableDisable(CommandContext& ctx,
                          const std::vector<QString>& targets,
                          const QString& instanceId,
                          bool recursive,
                          EnableAction action)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, instanceId, instance); code != ExitCode::OK) {
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

ExitCode runModCommand(CommandContext& ctx, const Cmd::ModList& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
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

ExitCode runModCommand(CommandContext& ctx, const Cmd::ModSearch& cmd)
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

    auto envelope = modrinthSearchJson(args, cmd.limit, cmd.offset);
    if (!envelope) {
        return outputError(QStringLiteral("network_error"), envelope.error(), ExitCode::Network);
    }
    outputJson(QJsonDocument(envelope.value()));
    return ExitCode::OK;
}

ExitCode runModCommand(CommandContext& ctx, const Cmd::ModInfo& cmd)
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
            // The version carries the Minecraft version twice - once as the
            // API returns it and once mapped to the version of the launcher.
            if (!mcVersions.contains(mcVersion)) {
                mcVersions.append(mcVersion);
            }
        }
        entry.insert(QStringLiteral("mc_versions"), mcVersions);
        QJsonArray loaders;
        for (const auto loader : ModPlatform::modLoaderTypesToList(version.loaders)) {
            loaders.append(ModPlatform::getModLoaderAsString(loader));
        }
        entry.insert(QStringLiteral("loaders"), loaders);
        versions.append(entry);
    }

    // The project of the API does not carry the author, so the team of the
    // project is fetched when the project has no authors.
    auto projectAuthors = pack.authors;
    if (projectAuthors.isEmpty()) {
        if (auto fetched = fetchProjectAuthors(pack.addonId.toString()); fetched) {
            projectAuthors = fetched.value();
        } else {
            qWarning() << "Could not fetch the team of the project:" << fetched.error();
        }
    }

    QJsonObject projectObject;
    projectObject.insert(QStringLiteral("slug"), pack.slug);
    projectObject.insert(QStringLiteral("project_id"), pack.addonId.toString());
    projectObject.insert(QStringLiteral("name"), pack.name);
    projectObject.insert(QStringLiteral("description"), pack.description);
    QJsonArray authors;
    for (const auto& author : projectAuthors) {
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
        if (auto code = resolveInstance(ctx.app, cmd.instance.value(), instance); code != ExitCode::OK) {
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

ExitCode runModCommand(CommandContext& ctx, const Cmd::ModInstall& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
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
        QList<ModPlatform::IndexedVersion> matches;
        for (const auto& version : allVersions) {
            if (version.fileId.toString() == wanted || version.versionNumber == wanted || version.version == wanted) {
                matches.append(version);
            }
        }
        // The version has to support the Minecraft version and the mod loader
        // of the instance - otherwise a mod for another loader would be
        // installed into the instance.
        QList<ModPlatform::IndexedVersion> usable;
        for (const auto& version : matches) {
            if (!version.mcVersion.contains(mcVersionString)) {
                continue;
            }
            if (!versionSupportsLoader(version, loaders)) {
                continue;
            }
            usable.append(version);
        }
        if (usable.isEmpty()) {
            return outputError(QStringLiteral("version_not_found"),
                               QStringLiteral("No version '%1' of '%2' supports Minecraft %3 and the mod loader of the instance")
                                   .arg(wanted, cmd.project, mcVersionString),
                               ExitCode::NotFound);
        }
        if (usable.size() > 1) {
            QJsonArray candidates;
            for (const auto& version : usable) {
                QJsonObject candidate;
                candidate.insert(QStringLiteral("version_id"), version.fileId.toString());
                candidate.insert(QStringLiteral("version_number"), version.versionNumber);
                candidate.insert(QStringLiteral("file_name"), version.fileName);
                candidates.append(candidate);
            }
            QJsonObject out;
            out.insert(QStringLiteral("candidates"), candidates);
            return outputError(QStringLiteral("ambiguous_target"),
                               QStringLiteral("The version '%1' matches more than one file").arg(wanted),
                               ExitCode::Ambiguous,
                               out);
        }
        selected = usable.first();
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

ExitCode runModCommand(CommandContext& ctx, const Cmd::ModRemove& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
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

ExitCode runModCommand(CommandContext& ctx, const Cmd::ModEnable& cmd)
{
    return runEnableDisable(ctx, cmd.targets, cmd.instance, cmd.recursive, EnableAction::ENABLE);
}

ExitCode runModCommand(CommandContext& ctx, const Cmd::ModDisable& cmd)
{
    return runEnableDisable(ctx, cmd.targets, cmd.instance, cmd.recursive, EnableAction::DISABLE);
}

/// The outcome of one `mod update` run - the arrays the command reports and
/// the flags that decide its exit code.
struct ModUpdateReport {
    QJsonArray updated;
    QJsonArray upToDate;
    QJsonArray skipped;
    QJsonArray failed;
    QJsonArray dependencies;
    bool hasUnresolvedTargets = false;
    bool hasCheckFailures = false;
};

/// Resolves the mods an update applies to - every mod (`--all`) or the mods
/// the targets point at. Targets that match nothing are recorded in the
/// report. Returns nothing when the resolution failed and the command has to
/// stop (the error is reported inside).
std::optional<QList<Resource*>> resolveUpdateTargets(ModFolderModel* model, const Cmd::ModUpdate& cmd, ModUpdateReport& report)
{
    if (cmd.all) {
        return model->allResources();
    }

    auto resolved = resolveTargets(model, cmd.targets, QStringLiteral("mod"));
    if (resolved.ambiguous) {
        QJsonObject out;
        out.insert(QStringLiteral("candidates"), resolved.candidates);
        out.insert(QStringLiteral("failed"), resolved.failed);
        outputError(QStringLiteral("ambiguous_target"),
                    QStringLiteral("Some targets matched more than one mod"),
                    ExitCode::Ambiguous,
                    out);
        return std::nullopt;
    }
    if (resolved.resources.isEmpty()) {
        QJsonObject out;
        out.insert(QStringLiteral("failed"), resolved.failed);
        outputError(QStringLiteral("not_found"),
                    QStringLiteral("None of the targets matched a mod"),
                    ExitCode::NotFound,
                    out);
        return std::nullopt;
    }

    // The targets that did not match any mod are reported as failed.
    report.failed = resolved.failed;
    report.hasUnresolvedTargets = !resolved.failed.isEmpty();
    return resolved.resources;
}

/// Mods that must not be updated - locked to a version (unless forced) and
/// folders - are recorded in the report, the rest is returned as updatable.
QList<Resource*> partitionUpdatable(const QList<Resource*>& candidates, bool force, ModUpdateReport& report)
{
    QList<Resource*> updatable;
    for (auto* resource : candidates) {
        if (resource->lockUpdate() && !force) {
            QJsonObject entry;
            entry.insert(QStringLiteral("name"), resource->name());
            entry.insert(QStringLiteral("file"), resource->fileinfo().fileName());
            entry.insert(QStringLiteral("reason"), QStringLiteral("the mod is locked to a specific version"));
            report.skipped.append(entry);
            continue;
        }
        if (resource->type() == ResourceType::FOLDER) {
            QJsonObject entry;
            entry.insert(QStringLiteral("name"), resource->name());
            entry.insert(QStringLiteral("file"), resource->fileinfo().fileName());
            entry.insert(QStringLiteral("reason"), QStringLiteral("folders cannot be updated"));
            report.skipped.append(entry);
            continue;
        }
        updatable.append(resource);
    }
    return updatable;
}

/// Make sure every updatable mod has metadata, otherwise it cannot be checked
/// for updates. The provider is taken from the managed pack, defaulting to
/// Modrinth (there is no interactive provider chooser in a headless run).
/// Mods that end up without metadata are recorded in the report, the rest is
/// returned as checkable.
std::optional<QList<Resource*>> ensureUpdateMetadata(ModFolderModel* model, const QList<Resource*>& updatable, ModUpdateReport& report)
{
    QList<Resource*> withoutMetadata;
    for (auto* resource : updatable) {
        if (!resource->metadata()) {
            withoutMetadata.append(resource);
        }
    }
    if (!withoutMetadata.empty()) {
        auto provider = ModPlatform::ResourceProvider::MODRINTH;
        auto metadataTask = makeShared<EnsureMetadataTask>(withoutMetadata, model->indexDir(), provider);
        auto sequence = makeShared<SequentialTask>(QStringLiteral("Looking for metadata"));
        if (auto hashingTask = metadataTask->getHashingTask()) {
            sequence->addTask(hashingTask);
        }
        if (auto result = runTaskSync(sequence, g_NetworkTimeoutMs); !result) {
            QJsonObject out;
            out.insert(QStringLiteral("failed"), report.failed);
            out.insert(QStringLiteral("reason"), result.error());
            outputError(QStringLiteral("metadata_failed"),
                        QStringLiteral("Could not generate the mod metadata: %1").arg(result.error()),
                        ExitCode::Network,
                        out);
            return std::nullopt;
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
            report.failed.append(entry);
            continue;
        }
        checkable.append(resource);
    }
    return checkable;
}

/// Checks the mods for updates and collects the downloads of the mods that
/// have a newer version. The dependencies are resolved (and reported), but
/// they are not downloaded - the update only replaces the mods that have a
/// newer version.
QList<shared_qobject_ptr<ResourceDownloadTask>> checkUpdatableMods(MinecraftInstance* instance,
                                                                   ModFolderModel* model,
                                                                   const QString& mcVersionString,
                                                                   QList<Resource*>& checkable,
                                                                   const Cmd::ModUpdate& cmd,
                                                                   ModUpdateReport& report)
{
    QList<shared_qobject_ptr<ResourceDownloadTask>> updateDownloads;
    if (checkable.isEmpty()) {
        return updateDownloads;
    }

    std::vector<Version> mcVersions{ Version(mcVersionString) };
    auto loaders = instance->getPackProfile()->getModLoadersList();

    auto checkTask = makeShared<ModrinthCheckUpdate>(checkable, mcVersions, loaders, model);

    // The context of the connections that are made below. It is declared
    // after the variables that the lambdas use, so that it is destroyed first
    // and the connections cannot outlive them.
    QObject connectionContext;

    // The mods for which the check failed - the check could not be done, so
    // they are neither updated nor up to date.
    QList<Resource*> failedChecks;
    QObject::connect(checkTask.get(),
                     &CheckUpdateTask::checkFailed,
                     &connectionContext,
                     [&failedChecks, &report](Resource* resource, const QString& reason, const QUrl&) {
                         if (!resource) {
                             return;
                         }
                         report.hasCheckFailures = true;
                         failedChecks.append(resource);
                         QJsonObject entry;
                         entry.insert(QStringLiteral("name"), resource->name());
                         entry.insert(QStringLiteral("file"), resource->fileinfo().fileName());
                         entry.insert(QStringLiteral("reason"), reason);
                         report.failed.append(entry);
                     });

    if (auto result = runTaskSync(checkTask, g_NetworkTimeoutMs); !result) {
        QJsonObject out;
        out.insert(QStringLiteral("failed"), report.failed);
        out.insert(QStringLiteral("reason"), result.error());
        outputError(QStringLiteral("check_failed"),
                    QStringLiteral("Could not check for updates: %1").arg(result.error()),
                    ExitCode::Network,
                    out);
        report.hasCheckFailures = true;
        return updateDownloads;
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
        report.updated.append(entry);
    }

    // The list that the check task returns holds the updated mods themselves -
    // it is the input of the dependency resolution.
    if (!cmd.noDeps && !checkTask->getDependencies().isEmpty()) {
        auto dependencyTask = makeShared<GetModDependenciesTask>(instance, model, checkTask->getDependencies());
        if (auto result = runTaskSync(dependencyTask, g_NetworkTimeoutMs); !result) {
            QJsonObject entry;
            entry.insert(QStringLiteral("reason"), QStringLiteral("the dependencies could not be resolved: %1").arg(result.error()));
            report.failed.append(entry);
            report.hasCheckFailures = true;
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
                report.dependencies.append(entry);
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
            report.upToDate.append(entry);
        }
    }
    return updateDownloads;
}

/// Downloads the updates (unless it is a dry run) and waits for the model to
/// settle. Returns nothing when the downloads failed and the command has to
/// stop (the error is reported inside).
std::optional<bool> applyUpdates(ModFolderModel* model,
                                 const QList<shared_qobject_ptr<ResourceDownloadTask>>& updateDownloads,
                                 const Cmd::ModUpdate& cmd,
                                 const ModUpdateReport& report)
{
    if (cmd.dryRun || updateDownloads.isEmpty()) {
        return true;
    }

    auto download = makeShared<ConcurrentTask>(
        QStringLiteral("Update Mods"), qMax(1, APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt()));
    for (const auto& update : updateDownloads) {
        download->addTask(update);
    }
    if (auto result = runTaskSync(download, g_NetworkTimeoutMs); !result) {
        QJsonObject out;
        out.insert(QStringLiteral("updated"), report.updated);
        out.insert(QStringLiteral("failed"), report.failed);
        out.insert(QStringLiteral("reason"), result.error());
        outputError(QStringLiteral("download_failed"),
                    QStringLiteral("Could not download the updates: %1").arg(result.error()),
                    ExitCode::Network,
                    out);
        return {};
    }
    waitForModel(model);
    return true;
}


ExitCode runModCommand(CommandContext& ctx, const Cmd::ModUpdate& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
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

    ModUpdateReport report;

    auto candidates = resolveUpdateTargets(model, cmd, report);
    if (!candidates) {
        return ExitCode::Generic;
    }
    auto updatable = partitionUpdatable(candidates.value(), cmd.force, report);
    auto checkable = ensureUpdateMetadata(model, updatable, report);
    if (!checkable) {
        return ExitCode::Network;
    }
    auto updateDownloads = checkUpdatableMods(instance, model, mcVersionString, checkable.value(), cmd, report);
    auto downloaded = applyUpdates(model, updateDownloads, cmd, report);
    if (!downloaded) {
        return ExitCode::Network;
    }

    QJsonObject out;
    out.insert(QStringLiteral("dry_run"), cmd.dryRun);
    out.insert(QStringLiteral("updated"), report.updated);
    out.insert(QStringLiteral("up_to_date"), report.upToDate);
    out.insert(QStringLiteral("skipped"), report.skipped);
    out.insert(QStringLiteral("failed"), report.failed);
    out.insert(QStringLiteral("dependencies"), report.dependencies);
    if (report.hasUnresolvedTargets) {
        // Some of the targets did not match a mod - the mods that were found
        // are updated anyway, but the command reports the failure.
        return outputError(QStringLiteral("not_found"),
                           QStringLiteral("Some of the targets did not match a mod"),
                           ExitCode::NotFound,
                           out);
    }
    if (report.hasCheckFailures) {
        // The update of at least one mod could not be checked - the command
        // reports the failure.
        return outputError(QStringLiteral("check_failed"),
                           QStringLiteral("The update of at least one mod could not be checked"),
                           ExitCode::Network,
                           out);
    }
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace Cli
