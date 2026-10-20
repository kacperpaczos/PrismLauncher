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

#include "cli/ScreenshotCommands.h"

#include <memory>
#include <optional>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "Application.h"
#include "FileSystem.h"
#include "cli/CommandUtils.h"
#include "cli/Output.h"
#include "cli/TaskRunner.h"
#include "minecraft/MinecraftInstance.h"
#include "net/NetJob.h"
#include "screenshots/ImgurAPI.h"
#include "screenshots/Screenshot.h"

namespace Cli {
namespace {

// Upper bound for the upload of the screenshots.
constexpr int g_NetworkTimeoutMs = 180000;
// The target that selects every screenshot of an instance.
constexpr auto g_AllScreenshots = "all";

/**
 * The screenshots of an instance, the newest one first.
 */
QList<ScreenShot::Ptr> loadScreenshots(MinecraftInstance* instance)
{
    QList<ScreenShot::Ptr> screenshots;
    QDir dir(FS::PathCombine(instance->gameRoot(), "screenshots"));
    const auto entries = dir.entryInfoList({ "*.png" }, QDir::Files | QDir::Readable, QDir::SortFlag::Time);
    for (const auto& entry : entries) {
        screenshots.append(std::make_shared<ScreenShot>(entry));
    }
    return screenshots;
}

/// The JSON of a screenshot of an instance.
QJsonObject screenshotToJson(const ScreenShot::Ptr& screenshot)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), screenshot->m_file.fileName());
    object.insert(QStringLiteral("file"), screenshot->m_file.absoluteFilePath());
    object.insert(QStringLiteral("size"), static_cast<qint64>(screenshot->m_file.size()));
    object.insert(QStringLiteral("modified"), jsonNullIfEmpty(isoDate(screenshot->m_file.lastModified())));
    return object;
}

/**
 * The JSON of an uploaded screenshot.
 *
 * The delete hash is reported as well - the launcher keeps it in memory
 * only, so it is the only chance to delete the screenshot again.
 */
QJsonObject uploadedToJson(const ScreenShot::Ptr& screenshot)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), screenshot->m_file.fileName());
    object.insert(QStringLiteral("url"), jsonNullIfEmpty(screenshot->m_url));
    object.insert(QStringLiteral("imgurId"), jsonNullIfEmpty(screenshot->m_imgurId));
    object.insert(QStringLiteral("deleteHash"), jsonNullIfEmpty(screenshot->m_imgurDeleteHash));
    return object;
}

}  // namespace

ExitCode runScreenshotCommand(ScreenshotContext& ctx, const Cmd::ScreenshotList& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }

    QJsonArray screenshots;
    for (const auto& screenshot : loadScreenshots(instance)) {
        screenshots.append(screenshotToJson(screenshot));
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("total"), static_cast<int>(screenshots.size()));
    out.insert(QStringLiteral("screenshots"), screenshots);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runScreenshotCommand(ScreenshotContext& ctx, const Cmd::ScreenshotUpload& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }

    if (!cmd.assumeYes) {
        // The screenshots are published - they can be seen by everybody.
        return outputError(QStringLiteral("confirmation_required"),
                           QStringLiteral("Uploading a screenshot publishes it - use --yes to confirm the upload"),
                           ExitCode::Usage);
    }

    const auto screenshots = loadScreenshots(instance);

    QList<ScreenShot::Ptr> selected;
    if (cmd.target == QLatin1String(g_AllScreenshots)) {
        selected = screenshots;
        if (cmd.limit.has_value() && selected.size() > *cmd.limit) {
            selected = selected.mid(0, *cmd.limit);
        }
    } else {
        for (const auto& screenshot : screenshots) {
            if (screenshot->m_file.fileName() == cmd.target) {
                selected.append(screenshot);
                break;
            }
        }
    }

    if (selected.isEmpty()) {
        return outputError(QStringLiteral("screenshot_not_found"),
                           QStringLiteral("No screenshot matching '%1' was found in the instance").arg(cmd.target),
                           ExitCode::NotFound);
    }

    auto job = makeShared<NetJob>("Screenshot upload", APPLICATION->network());
    // A headless run cannot ask whether a failed request should be retried.
    job->setAskRetry(false);
    for (const auto& screenshot : selected) {
        auto [request, result] = ImgurAPI::makeUpload(screenshot);
        job->addNetAction(request);
    }
    if (auto result = runTaskSync(job, g_NetworkTimeoutMs); !result) {
        return outputError(QStringLiteral("upload_failed"),
                           QStringLiteral("The screenshot could not be uploaded: %1").arg(result.error()),
                           ExitCode::Network);
    }

    QJsonObject album;
    if (selected.size() > 1) {
        // The album needs the delete hashes of the uploads, so it is created
        // after them.
        auto [request, result] = ImgurAPI::makeAlbum(selected);
        auto albumJob = makeShared<NetJob>("Imgur album creation", APPLICATION->network());
        albumJob->setAskRetry(false);
        albumJob->addNetAction(request);
        if (auto albumResult = runTaskSync(albumJob, g_NetworkTimeoutMs); !albumResult) {
            return outputError(QStringLiteral("upload_failed"),
                               QStringLiteral("The album of the screenshots could not be created: %1").arg(albumResult.error()),
                               ExitCode::Network);
        }
        album.insert(QStringLiteral("id"), result->id);
        album.insert(QStringLiteral("url"), QStringLiteral("https://imgur.com/a/%1").arg(result->id));
        album.insert(QStringLiteral("deleteHash"), jsonNullIfEmpty(result->deleteHash));
    }

    QJsonArray uploaded;
    for (const auto& screenshot : selected) {
        uploaded.append(uploadedToJson(screenshot));
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("total"), static_cast<int>(uploaded.size()));
    out.insert(QStringLiteral("uploaded"), uploaded);
    if (!album.isEmpty()) {
        out.insert(QStringLiteral("album"), album);
    }
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

ExitCode runScreenshotCommand(ScreenshotContext& ctx, const Cmd::ScreenshotCopy& cmd)
{
    MinecraftInstance* instance = nullptr;
    if (auto code = resolveInstance(ctx.app, cmd.instance, instance); code != ExitCode::OK) {
        return code;
    }

    QList<ScreenShot::Ptr> selected;
    for (const auto& screenshot : loadScreenshots(instance)) {
        if (screenshot->m_file.fileName() == cmd.id) {
            selected.append(screenshot);
            break;
        }
    }
    if (selected.isEmpty()) {
        return outputError(QStringLiteral("screenshot_not_found"),
                           QStringLiteral("No screenshot with the id '%1' was found in the instance").arg(cmd.id),
                           ExitCode::NotFound);
    }

    const auto source = selected.first()->m_file;
    const QFileInfo destination(cmd.destination);
    const auto target = destination.isDir() ? FS::PathCombine(destination.absoluteFilePath(), source.fileName())
                                             : destination.absoluteFilePath();

    if (QFile::exists(target) && !cmd.force) {
        return outputError(QStringLiteral("file_exists"),
                           QStringLiteral("'%1' already exists - use --force to overwrite it").arg(target),
                           ExitCode::Conflict);
    }

    if (QFile::exists(target) && !QFile::remove(target)) {
        return outputError(QStringLiteral("copy_failed"),
                           QStringLiteral("The existing file '%1' could not be replaced").arg(target),
                           ExitCode::Generic);
    }
    if (!QFile::copy(source.absoluteFilePath(), target)) {
        return outputError(QStringLiteral("copy_failed"),
                           QStringLiteral("The screenshot could not be copied to '%1'").arg(target),
                           ExitCode::Generic);
    }

    QJsonObject out;
    out.insert(QStringLiteral("instance"), cmd.instance);
    out.insert(QStringLiteral("id"), source.fileName());
    out.insert(QStringLiteral("source"), source.absoluteFilePath());
    out.insert(QStringLiteral("target"), target);
    outputJson(QJsonDocument(out));
    return ExitCode::OK;
}

}  // namespace Cli
