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

#include <QEventLoop>
#include <QTimer>

#include "Application.h"
#include "InstanceList.h"
#include "cli/TaskRunner.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/Resource.h"
#include "minecraft/mod/ResourceFolderModel.h"
#include "modplatform/ResourceAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "modplatform/modrinth/ModrinthPackIndex.h"
#include "net/ApiRequest.h"
#include "net/Mode.h"
#include "net/NetJob.h"

namespace Cli {
namespace {

// Upper bound for a single network request.
constexpr int g_NetworkTimeoutMs = 180000;
constexpr int g_SearchPageSize = 25;

/// Runs a paged Modrinth search - the endpoint hardcodes the page size, so
/// the results have to be requested page by page - and builds the JSON
/// envelope shared by the search commands. The raw response is used (instead
/// of the RPC sink of `searchProjectsTask`) because it also carries the total
/// number of hits and the download count of every project.
// Upper bound for the model (re)scan + parse wait.
constexpr int g_ModelWaitMs = 180000;

}  // namespace

NetJob::Ptr makeCliNetJob(const QString& name)
{
    auto job = makeShared<NetJob>(name, APPLICATION->network());
    job->setAskRetry(false);
    return job;
}


Result<QJsonObject, QString> modrinthSearchJson(ResourceAPI::SearchArgs args, int limit, int offset)
{
    const int requestedOffset = offset;
    QJsonArray results;
    int total = 0;

    while (results.size() < limit) {
        args.offset = offset;

        auto spec = ModrinthAPI::get().searchProjects(args);
        auto [action, response] = Net::ApiRequest::makeByteArray(spec.first.url, Net::Request::Option::AutoRetry);
        auto job = makeCliNetJob(QStringLiteral("Modrinth::Search"));
        job->addNetAction(action);

        if (auto result = runTaskSync(job, g_NetworkTimeoutMs); !result) {
            return std::unexpected(QStringLiteral("The Modrinth search failed: %1").arg(result.error()));
        }

        QJsonParseError parseError;
        auto document = QJsonDocument::fromJson(*response, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return std::unexpected(
                QStringLiteral("The Modrinth search returned an invalid response: %1").arg(parseError.errorString()));
        }

        auto object = document.object();
        total = object.value(QStringLiteral("total_hits")).toInt(total);
        auto hits = object.value(QStringLiteral("hits")).toArray();

        for (const auto& hit : hits) {
            if (results.size() >= limit) {
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
    out.insert(QStringLiteral("offset"), requestedOffset);
    out.insert(QStringLiteral("limit"), limit);
    out.insert(QStringLiteral("total"), total);
    out.insert(QStringLiteral("results"), results);
    return out;
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

QString normalizedSlug(const QString& slug)
{
    static const QString s_suffix = QStringLiteral(".pw.toml");
    if (slug.endsWith(s_suffix)) {
        return slug.left(slug.size() - s_suffix.size());
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

QJsonObject resourceToJson(const Resource* resource, bool withLockUpdate)
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
    if (withLockUpdate) {
        obj.insert(QStringLiteral("lock_update"), resource->lockUpdate());
    }

    return obj;
}

MinecraftInstance* resolveInstance(Application& app, const QString& id)
{
    if (id.isEmpty()) {
        return nullptr;
    }
    auto* instances = app.instances();
    if (!instances) {
        return nullptr;
    }
    return instances->getInstanceById(id);
}

ExitCode resolveInstance(Application& app, const QString& id, MinecraftInstance*& instance)
{
    instance = resolveInstance(app, id);
    if (!instance) {
        return outputError(QStringLiteral("instance_not_found"), QStringLiteral("No instance with the id '%1' was found").arg(id), ExitCode::NotFound);
    }
    return ExitCode::OK;
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

bool waitForModel(ResourceFolderModel* model)
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
    timeout.start(g_ModelWaitMs);
    model->update();
    loop.exec();

    return !timedOut;
}

namespace {

/// The HTTP status code of the first failed action of a net job, if any.
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

}  // namespace

Result<ModPlatform::IndexedPack> fetchProject(const QString& project)
{
    // The request must not ask for a retry - the retry dialog of the GUI
    // cannot be answered in a headless run.
    auto [job, response] = ModrinthAPI::get().getProjectTask(project, false, false);
    if (!job || !response) {
        return std::unexpected(QStringLiteral("could not create the request for '%1'").arg(project));
    }

    if (auto result = runTaskSync(job, g_NetworkTimeoutMs); !result) {
        if (netStatusCode(job) == 404) {
            return std::unexpected(QStringLiteral("the project '%1' was not found").arg(project));
        }
        return std::unexpected(result.error());
    }

    return *response;
}

Result<QVector<ModPlatform::IndexedVersion>> fetchProjectVersions(const ModPlatform::IndexedPack::Ptr& pack,
                                                                  ModPlatform::ResourceType resourceType)
{
    QVector<ModPlatform::IndexedVersion> versions;

    ResourceAPI::VersionSearchArgs args;
    args.pack = pack;
    args.resourceType = resourceType;
    args.includeChangelog = false;

    ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> callbacks;
    callbacks.onSucceed = [&versions](auto& result) { versions = result; };
    callbacks.onFail = [](const QString& reason, int) { qWarning() << "Could not load the project versions:" << reason; };

    auto task = ModrinthAPI::get().getProjectVersions(args, callbacks, false);
    if (!task) {
        return std::unexpected(QStringLiteral("could not create the version request"));
    }

    if (auto result = runTaskSync(task, g_NetworkTimeoutMs); !result) {
        return std::unexpected(result.error());
    }

    return versions;
}

QList<Resource*> matchTarget(ResourceFolderModel* model, const QString& target)
{
    const auto all = model->allResources();

    auto matches = [&all](const auto& predicate) {
        QList<Resource*> found;
        for (auto* resource : all) {
            if (predicate(resource)) {
                found.append(resource);
            }
        }
        return found;
    };

    auto byFileName = matches([&target](Resource* resource) {
        return resource->fileinfo().fileName() == target || resource->getOriginalFileName() == target;
    });
    if (!byFileName.isEmpty()) {
        return byFileName;
    }

    auto bySlug = matches([&target](Resource* resource) {
        auto meta = resource->metadata();
        return meta && normalizedSlug(meta->slug) == target;
    });
    if (!bySlug.isEmpty()) {
        return bySlug;
    }

    auto byProjectId = matches([&target](Resource* resource) {
        auto meta = resource->metadata();
        return meta && meta->projectId.isValid() && meta->projectId.toString() == target;
    });
    if (!byProjectId.isEmpty()) {
        return byProjectId;
    }

    return matches([&target](Resource* resource) { return resource->name() == target; });
}

ResolvedTargets resolveTargets(ResourceFolderModel* model, const std::vector<QString>& targets, const QString& noun, bool force)
{
    ResolvedTargets resolved;
    for (const auto& target : targets) {
        auto matches = matchTarget(model, target);
        if (matches.isEmpty()) {
            QJsonObject entry;
            entry.insert(QStringLiteral("target"), target);
            entry.insert(QStringLiteral("reason"), QStringLiteral("no %1 matched the target").arg(noun));
            resolved.failed.append(entry);
            continue;
        }
        if (matches.size() > 1) {
            QJsonObject entry;
            entry.insert(QStringLiteral("target"), target);
            QJsonArray candidateNames;
            for (auto* resource : matches) {
                QJsonObject candidate;
                candidate.insert(QStringLiteral("name"), resource->name());
                candidate.insert(QStringLiteral("file"), resource->fileinfo().fileName());
                auto meta = resource->metadata();
                candidate.insert(QStringLiteral("slug"), meta ? jsonNullIfEmpty(normalizedSlug(meta->slug)) : QJsonValue(QJsonValue::Null));
                candidateNames.append(candidate);
            }
            entry.insert(QStringLiteral("candidates"), candidateNames);
            resolved.candidates.append(entry);
            if (!force) {
                resolved.ambiguous = true;
                continue;
            }
        }
        for (auto* resource : matches) {
            if (!resolved.resources.contains(resource)) {
                resolved.resources.append(resource);
            }
        }
    }
    return resolved;
}

}  // namespace Cli
