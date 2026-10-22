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
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "cli/CommandUtils.h"
#include "cli/ModCommands.h"

using namespace Cli;

class CliModRestoreTest : public QObject {
    Q_OBJECT

   private slots:
    void restoresTheDownloadedFilesToDisabled()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QDir dir(tmp.path());

        QVERIFY(writeFile(dir, "A.jar.disabled", "old-a"));
        QVERIFY(writeFile(dir, "A.jar", "new-a"));
        QVERIFY(writeFile(dir, "B.jar.disabled", "old-b"));
        QVERIFY(writeFile(dir, "B.jar", "new-b"));

        const QList<DisabledModRename> renames{ { "Mod A", "A.jar.disabled", "A.jar" },
                                                { "Mod B", "B.jar.disabled", "B.jar" } };
        const auto failed = restoreDisabledMods(dir, renames);

        // Everything was restored - there is no failure to report.
        QCOMPARE(failed.size(), 0);
        // The downloaded files are disabled again, the old content is gone.
        QVERIFY(QFile::exists(dir.absoluteFilePath("A.jar.disabled")));
        QVERIFY(!QFile::exists(dir.absoluteFilePath("A.jar")));
        QCOMPARE(fileContent(dir, "A.jar.disabled"), QByteArray("new-a"));
        QVERIFY(QFile::exists(dir.absoluteFilePath("B.jar.disabled")));
        QVERIFY(!QFile::exists(dir.absoluteFilePath("B.jar")));
        QCOMPARE(fileContent(dir, "B.jar.disabled"), QByteArray("new-b"));
    }

    void skipsThePairsWhoseDownloadFailed()
    {
        // Regression: when one download of the batch failed, the restoration
        // used to be skipped for the whole batch - the successfully
        // downloaded disabled mod came back enabled. The decision has to be
        // made per pair, on what is actually on the disk.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QDir dir(tmp.path());

        // The download of A went through, the download of B failed - its
        // fresh file is not on the disk and its old `.disabled` file is
        // still in place.
        QVERIFY(writeFile(dir, "A.jar.disabled", "old-a"));
        QVERIFY(writeFile(dir, "A.jar", "new-a"));
        QVERIFY(writeFile(dir, "B.jar.disabled", "old-b"));

        const QList<DisabledModRename> renames{ { "Mod A", "A.jar.disabled", "A.jar" },
                                                { "Mod B", "B.jar.disabled", "B.jar" } };
        const auto failed = restoreDisabledMods(dir, renames);

        // A skipped pair is not a failure of the restoration - the failure
        // of the download is reported by the download itself.
        QCOMPARE(failed.size(), 0);
        // A was disabled again ...
        QVERIFY(QFile::exists(dir.absoluteFilePath("A.jar.disabled")));
        QVERIFY(!QFile::exists(dir.absoluteFilePath("A.jar")));
        QCOMPARE(fileContent(dir, "A.jar.disabled"), QByteArray("new-a"));
        // ... and B kept its old disabled file untouched.
        QVERIFY(QFile::exists(dir.absoluteFilePath("B.jar.disabled")));
        QVERIFY(!QFile::exists(dir.absoluteFilePath("B.jar")));
        QCOMPARE(fileContent(dir, "B.jar.disabled"), QByteArray("old-b"));
    }

    void reportsTheRestoresThatFailed()
    {
        // A rename that does not go through must not be a warning only - it
        // becomes an entry of the `failed` array, so that the exit code of
        // the command reflects the mod that came back enabled.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QDir dir(tmp.path());

        QVERIFY(writeFile(dir, "X.jar", "new-x"));
        // A directory at the path of the disabled file blocks both the
        // removal of the old file and the rename of the new one.
        QVERIFY(QDir(dir.absoluteFilePath("X.jar.disabled")).mkpath("."));

        const QList<DisabledModRename> renames{ { "Mod X", "X.jar.disabled", "X.jar" } };
        const auto failed = restoreDisabledMods(dir, renames);

        QCOMPARE(failed.size(), 2);
        for (const auto& entry : failed) {
            const auto object = entry.toObject();
            QCOMPARE(object.value("name").toString(), QStringLiteral("Mod X"));
            QCOMPARE(object.value("code").toString(), QString::fromLatin1(FailureCode::Io));
            QVERIFY(!object.value("reason").toString().isEmpty());
            QVERIFY(!object.value("file").toString().isEmpty());
        }
        // Nothing was renamed - the downloaded file is still in place.
        QVERIFY(QFile::exists(dir.absoluteFilePath("X.jar")));
    }

    void noRenamesIsNoop()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QDir dir(tmp.path());

        const auto failed = restoreDisabledMods(dir, {});
        QCOMPARE(failed.size(), 0);
    }

   private:
    static bool writeFile(const QDir& dir, const QString& name, const QByteArray& content)
    {
        QFile file(dir.absoluteFilePath(name));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        return file.write(content) == content.size();
    }

    static QByteArray fileContent(const QDir& dir, const QString& name)
    {
        QFile file(dir.absoluteFilePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        return file.readAll();
    }
};

QTEST_MAIN(CliModRestoreTest)
#include "CliModRestore_test.moc"
