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

#include "cli/PackCommands.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>

#include "Application.h"
#include "InstanceList.h"
#include "ResourceDownloadTask.h"
#include "Version.h"
#include "cli/CommandUtils.h"
#include "cli/TaskRunner.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/Resource.h"
#include "minecraft/mod/ResourceFolderModel.h"
#include "minecraft/mod/ResourcePack.h"
#include "minecraft/mod/ResourcePackFolderModel.h"
#include "minecraft/mod/ShaderPackFolderModel.h"
#include "minecraft/mod/TexturePackFolderModel.h"
#include "modplatform/ModIndex.h"
#include "modplatform/ResourceAPI.h"
#include "modplatform/ResourceType.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "modplatform/modrinth/ModrinthPackIndex.h"
#include "net/ApiRequest.h"
#include "net/NetJob.h"
#include "tasks/ConcurrentTask.h"

namespace Cli {
namespace {

// The Modrinth search API always uses a page size of 25, so the results have
// to be requested page by page.
constexpr int g_SearchPageSize = 25;
// Upper bound for a single network request.
constexpr int g_NetworkTimeoutMs = 180000;

/**
 * The properties of a pack kind.
 */
struct PackKind {
    /// The name of the kind, used in the messages and in the failure reasons of
    /// the JSON output.
    const char* noun;
    /// The key of the pack list in the JSON output.
    const char* listKey;
    /// The kind of resource the packs are.
    ModPlatform::ResourceType resourceType;
    /// The Modrinth project type of the packs. Modrinth does not have a
    /// project type for texture packs - they are published as resource packs,
    /// so the resource pack type is used for them.
    ModPlatform::ResourceType modrinthType;
};

const PackKind& packKind(Cmd::PackGroup group)
{
    static const std::array<PackKind, 1> kinds{ {
        { "resource pack", "resource_packs", ModPlatform::ResourceType::ResourcePack, ModPlatform::ResourceType::ResourcePack },
    } };
    return kinds[static_cast<std::size_t>(group)];
}

/**
 * The name of a pack kind.
 */
QString packNoun(Cmd::PackGroup group)
{
    return QString::fromLatin1(packKind(group).noun);
}

/**
 * The key of the pack list in the JSON output.
 */
QString packListKey(Cmd::PackGroup group)
{
    return QString::fromLatin1(packKind(group).listKey);
}

/**
 * The model of the instance that holds the packs of the given kind.
 */
ResourceFolderModel* packModel(MinecraftInstance* instance, Cmd::PackGroup group)
{
    switch (group) {
        case Cmd::PackGroup::Resource:
            return instance->resourcePackList();
    }
    return nullptr;
}

/**
 * Serialize a single installed pack the way the CLI reports it. The pack
 * report has no `lock_update` field - only the mod report has it.
 */
QJsonObject packToJson(const Resource* resource, Cmd::PackGroup group)
{
    QJsonObject object = resourceToJson(resource, false);
    if (group == Cmd::PackGroup::Resource) {
        // The pack format of a resource pack is read from its pack.mcmeta by
        // the parse task of the model.
        const auto* pack = dynamic_cast<const ResourcePack*>(resource);
        const int packFormat = pack ? pack->packFormat() : 0;
        object.insert(QStringLiteral("pack_format"), packFormat > 0 ? QJsonValue(packFormat) : QJsonValue(QJsonValue::Null));
    }
    return object;
}

/**
 * Resolve the instance of a pack command. The error is reported and the exit
 * code is returned when the instance does not exist.
 */
ExitCode resolvePackInstance(PackContext& ctx, const QString& instanceId, MinecraftInstance*& instance)
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
 * Open the pack model of an instance and wait for it to be up to date. The
 * error is reported and the exit code is returned when it fails.
 */
ExitCode openPackModel(MinecraftInstance* instance, const QString& instanceId, Cmd::PackGroup group, ResourceFolderModel*& model)
{
    model = packModel(instance, group);
    if (!model) {
        return outputError(QStringLiteral("no_pack_list"),
                           QStringLiteral("The instance has no %1 list").arg(packNoun(group)),
                           ExitCode::Generic);
    }
    if (!waitForModel(model)) {
        return outputError(QStringLiteral("model_timeout"),
                           QStringLiteral("Timed out while reading the %1s of the instance '%2'").arg(packNoun(group), instanceId),
                           ExitCode::Generic);
    }
    return ExitCode::OK;
}

/**
 * A mutating command refuses to work on a running instance. The hint tells
 * the user how the command can be forced.
 */
ExitCode checkInstanceRunning(MinecraftInstance* instance, const QString& instanceId, const QString& action, const QString& hint = {})
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

/**
 * A pack that is being (or has been) downloaded.
 */
struct DownloadEntry {
    QString slug;
    QString fileName;
    QString projectId;
    QString versionId;
    QString versionNumber;
    QString releaseType;
    QString source;
};

/**
 * The JSON of a downloaded pack. It is used when the pack is not in the model
 * or when it has no metadata (because it was installed with `--no-metadata`).
 */
QJsonObject downloadedPackToJson(const DownloadEntry& entry)
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
 * The row of a resource in the model.
 */
int rowOfResource(ResourceFolderModel* model, const Resource* resource)
{
    const auto all = model->allResources();
    for (int row = 0; row < all.size(); ++row) {
        if (all.at(row) == resource) {
            return row;
        }
    }
    return -1;
}

/**
 * Shared implementation of `enable` and `disable`.
 */
ExitCode runEnableDisable(PackContext& ctx,
                          Cmd::PackGroup group,
                          const std::vector<QString>& targets,
                          const QString& instanceId,
                          EnableAction action)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolvePackInstance(ctx, instanceId, instance); code != ExitCode::OK) {
        return code;
    }
    if (auto code = checkInstanceRunning(instance, instanceId, QStringLiteral("change %1s").arg(packNoun(group))); code != ExitCode::OK) {
        return code;
    }

    ResourceFolderModel* model = nullptr;
    if (auto code = openPackModel(instance, instanceId, group, model); code != ExitCode::OK) {
        return code;
    }

    auto resolved = resolveTargets(model, targets, packNoun(group));
    if (resolved.ambiguous) {
        QJsonObject out;
        out.insert(QStringLiteral("candidates"), resolved.candidates);
        out.insert(QStringLiteral("failed"), resolved.failed);
        return outputError(QStringLiteral("ambiguous_target"),
                           QStringLiteral("Some targets matched more than one %1").arg(packNoun(group)),
                           ExitCode::Ambiguous,
                           out);
    }
    if (resolved.resources.isEmpty()) {
        QJsonObject out;
        out.insert(QStringLiteral("changed"), QJsonArray());
        out.insert(QStringLiteral("failed"), resolved.failed);
        return outputError(QStringLiteral("not_found"),
                           QStringLiteral("None of the targets matched a %1").arg(packNoun(group)),
                           ExitCode::NotFound,
                           out);
    }

    QJsonArray changed;
    QJsonArray failed = resolved.failed;
    for (auto* resource : resolved.resources) {
        const bool wantEnabled = action == EnableAction::ENABLE;
        if (resource->enabled() == wantEnabled) {
            // The pack is already in the wanted state - nothing to do.
            continue;
        }
        const int row = rowOfResource(model, resource);
        if (row < 0 || !model->setResourceEnabled({ model->index(row, 0) }, action)) {
            QJsonObject entry;
            entry.insert(QStringLiteral("target"), resource->fileinfo().fileName());
            entry.insert(QStringLiteral("reason"), QStringLiteral("the %1 could not be %2").arg(packNoun(group), wantEnabled ? "enabled" : "disabled"));
            failed.append(entry);
            continue;
        }
        changed.append(packToJson(resource, group));
    }

    QJsonObject out;
    out.insert(QStringLiteral("changed"), changed);
    out.insert(QStringLiteral("failed"), failed);
    if (changed.isEmpty() && !failed.isEmpty()) {
        return outputError(QStringLiteral("change_failed"),
                           QStringLiteral("None of the %1s could be changed").arg(packNoun(group)),
                           ExitCode::Generic,
                           out);
    }
    if (!resolved.failed.isEmpty()) {
        // Some of the targets did not match a pack - the packs that were found
        // are changed anyway, but the command reports the failure.
        return outputError(QStringLiteral("not_found"),
                           QStringLiteral("Some of the targets did not match a %1").arg(packNoun(group)),
                           ExitCode::NotFound,
                           out);
    }
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace

ExitCode runPackCommand(PackContext& ctx, const Cmd::PackList& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolvePackInstance(ctx, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }

    ResourceFolderModel* model = nullptr;
    if (auto code = openPackModel(instance, cmd.instance, cmd.group, model); code != ExitCode::OK) {
        return code;
    }

    QJsonArray packs;
    for (auto* resource : model->allResources()) {
        if (!cmd.includeDisabled && !resource->enabled()) {
            continue;
        }
        packs.append(packToJson(resource, cmd.group));
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("total"), static_cast<int>(packs.size()));
    out.insert(packListKey(cmd.group), packs);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runPackCommand(PackContext& ctx, const Cmd::PackSearch& cmd)
{
    ResourceAPI::SearchArgs args;
    args.type = packKind(cmd.group).modrinthType;
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

ExitCode runPackCommand(PackContext& ctx, const Cmd::PackInfo& cmd)
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

    auto versionsResult = fetchProjectVersions(packPtr, packKind(cmd.group).resourceType);
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

    // The installed pack is looked up by slug or project id.
    QJsonValue installed = QJsonValue(QJsonValue::Null);
    if (cmd.instance.has_value()) {
        MinecraftInstance* instance = nullptr;
        if (auto code = resolvePackInstance(ctx, cmd.instance.value(), instance); code != ExitCode::OK) {
            return code;
        }
        ResourceFolderModel* model = nullptr;
        if (openPackModel(instance, cmd.instance.value(), cmd.group, model) == ExitCode::OK) {
            for (auto* resource : model->allResources()) {
                auto meta = resource->metadata();
                if (!meta) {
                    continue;
                }
                if (normalizedSlug(meta->slug) == pack.slug ||
                    (meta->projectId.isValid() && meta->projectId.toString() == pack.addonId.toString())) {
                    installed = packToJson(resource, cmd.group);
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

ExitCode runPackCommand(PackContext& ctx, const Cmd::PackInstall& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolvePackInstance(ctx, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }
    if (auto code = checkInstanceRunning(instance, cmd.instance, QStringLiteral("install %1s").arg(packNoun(cmd.group)));
        code != ExitCode::OK) {
        return code;
    }

    ResourceFolderModel* model = nullptr;
    if (auto code = openPackModel(instance, cmd.instance, cmd.group, model); code != ExitCode::OK) {
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

    auto project = fetchProject(cmd.project);
    if (!project) {
        if (project.error().contains(QStringLiteral("was not found"))) {
            return outputError(QStringLiteral("project_not_found"), project.error(), ExitCode::NotFound);
        }
        return outputError(QStringLiteral("network_error"), project.error(), ExitCode::Network);
    }
    auto packPtr = std::make_shared<ModPlatform::IndexedPack>(project.value());

    auto versionsResult = fetchProjectVersions(packPtr, packKind(cmd.group).resourceType);
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
            selected = version;
            break;
        }
        if (!selected.has_value()) {
            // Some packs do not tell which Minecraft versions they support -
            // the newest one is used for them.
            for (const auto& version : allVersions) {
                if (!version.mcVersion.isEmpty()) {
                    continue;
                }
                selected = version;
                break;
            }
        }
        if (!selected.has_value()) {
            return outputError(QStringLiteral("version_not_found"),
                               QStringLiteral("No version of '%1' was found for Minecraft %2").arg(cmd.project, mcVersionString),
                               ExitCode::NotFound);
        }
    }

    // The failure list is shared with the download task, which may outlive
    // this function (the tasks are deleted through the event loop).
    auto failed = std::make_shared<QJsonArray>();
    auto download = makeShared<ConcurrentTask>(
        QStringLiteral("Download %1s").arg(packNoun(cmd.group)),
        qMax(1, APPLICATION->settings()->get("NumberOfConcurrentDownloads").toInt()));

    auto task = makeShared<ResourceDownloadTask>(packPtr, selected.value(), model, !cmd.noMetadata, "install");
    QObject::connect(task.get(), &Task::failed, download.get(), [packPtr, failed](const QString& reason) {
        QJsonObject entry;
        entry.insert(QStringLiteral("slug"), packPtr->slug);
        entry.insert(QStringLiteral("reason"), reason);
        failed->append(entry);
    });
    download->addTask(task);

    DownloadEntry entry{ .slug = packPtr->slug,
                         .fileName = selected.value().fileName,
                         .projectId = packPtr->addonId.toString(),
                         .versionId = selected.value().fileId.toString(),
                         .versionNumber = selected.value().versionNumber,
                         .releaseType = selected.value().versionType.isValid() ? selected.value().versionType.toString() : QString(),
                         .source = sourceName(packPtr->provider) };

    if (auto result = runTaskSync(download, g_NetworkTimeoutMs); !result) {
        QJsonObject out;
        out.insert(QStringLiteral("failed"), *failed);
        out.insert(QStringLiteral("reason"), result.error());
        return outputError(QStringLiteral("download_failed"), result.error(), ExitCode::Network, out);
    }

    // Refresh the model so the freshly installed pack shows up in the output.
    waitForModel(model);

    QJsonObject object;
    auto* resource = findResourceByFileName(model, entry.fileName);
    object = resource ? packToJson(resource, cmd.group) : QJsonObject();
    if (object.isEmpty()) {
        // The pack was not picked up by the model, so the data of the download
        // is used instead.
        object = downloadedPackToJson(entry);
    } else if (!resource->metadata()) {
        // The pack was installed without metadata, so the metadata of the
        // download is reported instead of the missing one.
        const auto downloaded = downloadedPackToJson(entry);
        for (auto it = downloaded.constBegin(); it != downloaded.constEnd(); ++it) {
            if (it.key() == QLatin1String("name") || it.key() == QLatin1String("file") ||
                it.key() == QLatin1String("enabled") || it.key() == QLatin1String("date") ||
                it.key() == QLatin1String("hash")) {
                continue;
            }
            object.insert(it.key(), it.value());
        }
    }

    QJsonObject out;
    out.insert(QStringLiteral("installed"), QJsonArray{ object });
    out.insert(QStringLiteral("failed"), *failed);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runPackCommand(PackContext& ctx, const Cmd::PackRemove& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolvePackInstance(ctx, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }
    if (!cmd.force) {
        if (auto code = checkInstanceRunning(instance, cmd.instance, QStringLiteral("remove %1s").arg(packNoun(cmd.group)),
                                             QStringLiteral("use --force"));
            code != ExitCode::OK) {
            return code;
        }
    }

    ResourceFolderModel* model = nullptr;
    if (auto code = openPackModel(instance, cmd.instance, cmd.group, model); code != ExitCode::OK) {
        return code;
    }

    auto resolved = resolveTargets(model, cmd.targets, packNoun(cmd.group), cmd.force);
    if (resolved.ambiguous) {
        QJsonObject out;
        out.insert(QStringLiteral("candidates"), resolved.candidates);
        out.insert(QStringLiteral("failed"), resolved.failed);
        return outputError(QStringLiteral("ambiguous_target"),
                           QStringLiteral("Some targets matched more than one %1").arg(packNoun(cmd.group)),
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
            entry.insert(QStringLiteral("reason"),
                         QStringLiteral("the %1 is locked to a specific version (use --force)").arg(packNoun(cmd.group)));
            failed.append(entry);
            lockedRefused = true;
            continue;
        }
        // The metadata is gone once the pack is destroyed, so it has to be
        // read before.
        auto meta = resource->metadata();
        auto slug = meta ? normalizedSlug(meta->slug) : QString();
        if (!resource->destroy(indexDir, false, true)) {
            QJsonObject entry;
            entry.insert(QStringLiteral("target"), fileName);
            entry.insert(QStringLiteral("reason"), QStringLiteral("the %1 could not be removed").arg(packNoun(cmd.group)));
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
                               QStringLiteral("None of the targets matched a %1").arg(packNoun(cmd.group)),
                               ExitCode::NotFound,
                               out);
        }
        if (lockedRefused) {
            return outputError(QStringLiteral("locked"),
                               QStringLiteral("The %1 is locked to a specific version (use --force to remove it anyway)")
                                   .arg(packNoun(cmd.group)),
                               ExitCode::Conflict,
                               out);
        }
        return outputError(QStringLiteral("remove_failed"),
                           QStringLiteral("None of the %1s could be removed").arg(packNoun(cmd.group)),
                           ExitCode::Generic,
                           out);
    }
    if (!resolved.failed.isEmpty()) {
        // Some of the targets did not match a pack - the packs that were found
        // are removed anyway, but the command reports the failure.
        return outputError(QStringLiteral("not_found"),
                           QStringLiteral("Some of the targets did not match a %1").arg(packNoun(cmd.group)),
                           ExitCode::NotFound,
                           out);
    }
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runPackCommand(PackContext& ctx, const Cmd::PackEnable& cmd)
{
    return runEnableDisable(ctx, cmd.group, cmd.targets, cmd.instance, EnableAction::ENABLE);
}

ExitCode runPackCommand(PackContext& ctx, const Cmd::PackDisable& cmd)
{
    return runEnableDisable(ctx, cmd.group, cmd.targets, cmd.instance, EnableAction::DISABLE);
}

}  // namespace Cli
