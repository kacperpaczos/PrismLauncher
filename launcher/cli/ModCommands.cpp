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
#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include "Application.h"
#include "InstanceList.h"
#include "ResourceDownloadTask.h"
#include "Version.h"
#include "cli/TaskRunner.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/MetadataHandler.h"
#include "minecraft/mod/ModFolderModel.h"
#include "minecraft/mod/ResourceFolderModel.h"
#include "minecraft/mod/tasks/GetModDependenciesTask.h"
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
constexpr int kSearchPageSize = 25;
// Upper bound for the model (re)scan + parse wait.
constexpr int kModelWaitMs = 180000;
// Upper bound for a single network request.
constexpr int kNetworkTimeoutMs = 180000;

ExitCode notImplemented(const QString& subcommand)
{
    return outputError(QStringLiteral("not_implemented"), QStringLiteral("`mod %1` is not implemented yet").arg(subcommand));
}

QJsonValue jsonNullIfEmpty(const QString& value)
{
    return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
}

QJsonValue jsonNullIfEmpty(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return QJsonValue(QJsonValue::Null);
    }
    return QJsonValue(value.toString());
}

QString sourceName(ModPlatform::ResourceProvider provider)
{
    switch (provider) {
        case ModPlatform::ResourceProvider::MODRINTH:
            return QStringLiteral("modrinth");
        case ModPlatform::ResourceProvider::FLAME:
            return QStringLiteral("flame");
    }
    return QStringLiteral("local");
}

/**
 * The metadata of a mod is loaded with the name of the index file, so the
 * slug of a mod can carry the `.pw.toml` suffix. The suffix is not part of
 * the slug of the mod, so it is stripped for the output.
 */
QString normalizedSlug(const QString& slug)
{
    static const QString suffix = QStringLiteral(".pw.toml");
    if (slug.endsWith(suffix)) {
        return slug.left(slug.size() - suffix.size());
    }
    return slug;
}

QString isoDate(const QDateTime& date)
{
    if (!date.isValid()) {
        return {};
    }
    return date.toUTC().toString(Qt::ISODate);
}

/**
 * Serialize a single installed resource the way the CLI reports it.
 */
QJsonObject modToJson(const Resource* resource)
{
    QJsonObject obj;
    if (!resource) {
        return obj;
    }

    auto meta = resource->metadata();

    obj.insert(QStringLiteral("name"), resource->name());
    obj.insert(QStringLiteral("slug"), meta ? jsonNullIfEmpty(normalizedSlug(meta->slug)) : QJsonValue(QJsonValue::Null));
    obj.insert(QStringLiteral("project_id"), meta ? jsonNullIfEmpty(meta->projectId) : QJsonValue(QJsonValue::Null));
    obj.insert(QStringLiteral("version_id"), meta ? jsonNullIfEmpty(meta->fileId) : QJsonValue(QJsonValue::Null));
    obj.insert(QStringLiteral("version_number"), meta ? jsonNullIfEmpty(meta->versionNumber) : QJsonValue(QJsonValue::Null));
    obj.insert(QStringLiteral("file"), resource->fileinfo().fileName());
    obj.insert(QStringLiteral("enabled"), resource->enabled());
    obj.insert(QStringLiteral("date"), jsonNullIfEmpty(isoDate(resource->dateTimeChanged())));

    QJsonObject hash;
    hash.insert(QStringLiteral("sha1"), QJsonValue(QJsonValue::Null));
    hash.insert(QStringLiteral("sha512"), QJsonValue(QJsonValue::Null));
    if (meta && !meta->hash.isEmpty()) {
        if (meta->hashFormat.compare(QStringLiteral("sha1"), Qt::CaseInsensitive) == 0) {
            hash.insert(QStringLiteral("sha1"), meta->hash);
        } else if (meta->hashFormat.compare(QStringLiteral("sha512"), Qt::CaseInsensitive) == 0) {
            hash.insert(QStringLiteral("sha512"), meta->hash);
        }
    }
    obj.insert(QStringLiteral("hash"), hash);

    obj.insert(QStringLiteral("source"), meta ? sourceName(meta->provider) : QStringLiteral("local"));
    obj.insert(QStringLiteral("release_type"),
               meta ? jsonNullIfEmpty(meta->releaseType.isValid() ? meta->releaseType.toString() : QString())
                    : QJsonValue(QJsonValue::Null));
    obj.insert(QStringLiteral("lock_update"), resource->lockUpdate());

    return obj;
}

/**
 * Look up an instance by id. Returns nullptr when it does not exist.
 */
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

/**
 * Make sure the component profile of the instance is loaded. The GUI does
 * that when the instance is opened, a headless run has to do it itself.
 */
/**
 * Wait for the resource model to finish its (re)scan and all pending parse
 * tasks. The model update runs on a thread pool, so a plain event loop with a
 * polling timer is used here - the same approach the GUI takes with its
 * progress dialog.
 */
bool waitForModel(ResourceFolderModel* model, int timeoutMs = kModelWaitMs)
{
    if (!model) {
        return false;
    }

    bool updated = false;
    QEventLoop loop;
    QObject::connect(model, &ResourceFolderModel::updateFinished, &loop, [&updated] { updated = true; });

    QTimer poll;
    poll.setInterval(20);
    QObject::connect(&poll, &QTimer::timeout, &loop, [model, &updated, &loop] {
        if (updated && !model->hasPendingParseTasks()) {
            loop.quit();
        }
    });

    QTimer timeout;
    timeout.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&timedOut, &loop] {
        timedOut = true;
        loop.quit();
    });

    poll.start();
    timeout.start(timeoutMs);
    model->update();
    loop.exec();

    return !timedOut;
}

/**
 * Find a resource by the name of the file it is stored in.
 */
/**
 * Resolve a user provided target (file name, slug, project id or display name)
 * into the matching resources. The lookup order is:
 *   1. exact file name (with or without the `.disabled` suffix)
 *   2. metadata slug
 *   3. metadata project id
 *   4. display name
 */
/**
 * The HTTP status code of the first failed action of a net job, if any.
 */
int netStatusCode(const NetJob::Ptr& job)
{
    if (!job) {
        return -1;
    }
    for (auto* action : job->getFailedActions()) {
        if (action) {
            return action->replyStatusCode();
        }
    }
    return -1;
}

/**
 * Resolve a project (slug or id) on Modrinth.
 */
Result<ModPlatform::IndexedPack> fetchProject(const QString& project)
{
    auto [job, response] = ModrinthAPI::get().getProjectTask(project);
    if (!job || !response) {
        return std::unexpected(QStringLiteral("could not create the request for '%1'").arg(project));
    }

    if (auto result = runTaskSync(job, kNetworkTimeoutMs); !result) {
        if (netStatusCode(job) == 404) {
            return std::unexpected(QStringLiteral("the project '%1' was not found").arg(project));
        }
        return std::unexpected(result.error());
    }

    return *response;
}

/**
 * Fetch all versions of a project. The list is sorted by date, newest first.
 */
Result<QVector<ModPlatform::IndexedVersion>> fetchProjectVersions(const ModPlatform::IndexedPack::Ptr& pack)
{
    QVector<ModPlatform::IndexedVersion> versions;

    ResourceAPI::VersionSearchArgs args;
    args.pack = pack;
    args.resourceType = ModPlatform::ResourceType::Mod;
    args.includeChangelog = false;

    ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> callbacks;
    callbacks.onSucceed = [&versions](auto& result) { versions = result; };
    callbacks.onFail = [](const QString& reason, int) { qWarning() << "Could not load the project versions:" << reason; };

    auto task = ModrinthAPI::get().getProjectVersions(args, callbacks);
    if (!task) {
        return std::unexpected(QStringLiteral("could not create the version request"));
    }

    if (auto result = runTaskSync(task, kNetworkTimeoutMs); !result) {
        return std::unexpected(result.error());
    }

    return versions;
}

/**
 * The result of resolving the targets of the remove/enable/disable/update
 * commands.
 */
/**
 * Collect the mods that depend on the given ones (or that are required by
 * them). The relation graph of the model is used, the same way the GUI does
 * it for the "disable dependants" prompt.
 */
/**
 * Shared implementation of `mod enable` and `mod disable`.
 */
}  // namespace

ExitCode runModCommand(ModContext& ctx, const Cmd::ModList& cmd)
{
    auto* instance = resolveInstance(ctx, cmd.instance);
    if (!instance) {
        return outputError(QStringLiteral("instance_not_found"),
                           QStringLiteral("No instance with the id '%1' was found").arg(cmd.instance),
                           ExitCode::NotFound);
    }

    auto* model = instance->loaderModList();
    if (!model) {
        return outputError(QStringLiteral("no_mod_list"), QStringLiteral("The instance has no mod list"), ExitCode::Generic);
    }

    if (!waitForModel(model)) {
        return outputError(QStringLiteral("model_timeout"),
                           QStringLiteral("Timed out while reading the mods of the instance '%1'").arg(cmd.instance),
                           ExitCode::Generic);
    }

    QJsonArray mods;
    for (auto* resource : model->allResources()) {
        if (!cmd.includeDisabled && !resource->enabled()) {
            continue;
        }
        mods.append(modToJson(resource));
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("total"), static_cast<int>(mods.size()));
    out.insert(QStringLiteral("mods"), mods);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModSearch& /*cmd*/)
{
    return notImplemented(QStringLiteral("search"));
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

    auto versionsResult = fetchProjectVersions(packPtr);
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
        auto* instance = resolveInstance(ctx, cmd.instance.value());
        if (!instance) {
            return outputError(QStringLiteral("instance_not_found"),
                               QStringLiteral("No instance with the id '%1' was found").arg(cmd.instance.value()),
                               ExitCode::NotFound);
        }
        auto* model = instance->loaderModList();
        if (model && waitForModel(model)) {
            for (auto* resource : model->allResources()) {
                auto meta = resource->metadata();
                if (!meta) {
                    continue;
                }
                if (normalizedSlug(meta->slug) == pack.slug ||
                    (meta->projectId.isValid() && meta->projectId.toString() == pack.addonId.toString())) {
                    installed = modToJson(resource);
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

ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModInstall& /*cmd*/)
{
    return notImplemented(QStringLiteral("install"));
}
ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModRemove& /*cmd*/)
{
    return notImplemented(QStringLiteral("remove"));
}
ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModEnable& /*cmd*/)
{
    return notImplemented(QStringLiteral("enable"));
}
ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModDisable& /*cmd*/)
{
    return notImplemented(QStringLiteral("disable"));
}
ExitCode runModCommand(ModContext& /*ctx*/, const Cmd::ModUpdate& /*cmd*/)
{
    return notImplemented(QStringLiteral("update"));
}
}  // namespace Cli
