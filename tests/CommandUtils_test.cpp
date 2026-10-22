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

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include "Application.h"
#include "cli/CommandUtils.h"

using namespace Cli;

class CommandUtilsTest : public QObject {
    Q_OBJECT

   private slots:
    void resolveClientPath_passesUrlsThrough()
    {
        // Regression (P1): a URL used to be treated as a relative FILENAME and
        // was prefixed with the working directory of the client, so
        // `instance create --from https://...` imported from a bogus local
        // path that has no URL scheme.
        setClientWorkingDir(QStringLiteral("/tmp/launcher-data"));

        const QStringList urls{ QStringLiteral("https://example.com/pack.mrpack"),
                               QStringLiteral("http://example.com/pack.mrpack"),
                               QStringLiteral("file:///home/user/pack.mrpack") };
        for (const auto& url : urls) {
            QCOMPARE(resolveClientPath(url), url);
        }
    }

    void resolveClientPath_resolvesRelativePathsAgainstTheClient()
    {
        // Policy: the paths of the client are relative to the directory the
        // client was started in - not to the data path of the launcher.
        QTemporaryDir data;
        QVERIFY(data.isValid());
        setClientWorkingDir(data.path());

        QCOMPARE(resolveClientPath(QStringLiteral("pack.mrpack")),
                 QDir(data.path()).absoluteFilePath(QStringLiteral("pack.mrpack")));
        QCOMPARE(resolveClientPath(QStringLiteral("./pack.mrpack")),
                 QDir(data.path()).absoluteFilePath(QStringLiteral("./pack.mrpack")));
        QCOMPARE(resolveClientPath(QStringLiteral("../packs/x.zip")),
                 QDir(data.path()).absoluteFilePath(QStringLiteral("../packs/x.zip")));

        // An absolute path is returned as it is.
        const auto absolute = QDir(data.path()).absoluteFilePath(QStringLiteral("pack.mrpack"));
        QCOMPARE(resolveClientPath(absolute), absolute);

        // Without a remembered working directory there is nothing to resolve against.
        setClientWorkingDir(QString());
        QCOMPARE(resolveClientPath(QStringLiteral("pack.mrpack")), QStringLiteral("pack.mrpack"));

        setClientWorkingDir(data.path());
    }

    void normalizeImportUrl_keepsUrlSourcesIntact()
    {
        // The import of `instance create` normalizes its source like the
        // legacy `--import` does: a URL is not a file.
        setClientWorkingDir(QStringLiteral("/tmp/launcher-data"));

        const auto url = Application::normalizeImportUrl(QStringLiteral("https://example.com/pack.mrpack"));
        QCOMPARE(url, QUrl(QStringLiteral("https://example.com/pack.mrpack")));
        QCOMPARE(url.scheme(), QStringLiteral("https"));
    }

    void normalizeImportUrl_resolvesExistingFilesAgainstTheClient()
    {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QFile pack(QDir(data.path()).absoluteFilePath(QStringLiteral("pack.mrpack")));
        QVERIFY(pack.open(QIODevice::WriteOnly));
        pack.close();

        setClientWorkingDir(data.path());
        const auto url = Application::normalizeImportUrl(QStringLiteral("pack.mrpack"));
        QVERIFY(url.isLocalFile());
        QCOMPARE(url, QUrl::fromLocalFile(pack.fileName()));
    }
};

QTEST_GUILESS_MAIN(CommandUtilsTest)
#include "CommandUtils_test.moc"
