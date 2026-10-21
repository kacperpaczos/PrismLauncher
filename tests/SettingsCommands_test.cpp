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

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "cli/SettingsCommands.h"

class SettingsCommandsTest : public QObject {
    Q_OBJECT

  private slots:
    void test_notWritableSettingsIds()
    {
        const auto ids = Cli::notWritableSettingsIds();
        for (const auto* id : { "uuid", "InstanceType", "name", "notes", "lastLaunchTime", "totalTimePlayed", "lastTimePlayed" }) {
            QVERIFY2(ids.contains(QString::fromLatin1(id)), id);
            QVERIFY2(!Cli::isWritableSettingsId(QString::fromLatin1(id)), id);
        }

        // the settings of a managed pack are not in the list - they all start
        // with `ManagedPack`
        for (const auto* id : { "ManagedPack", "ManagedPackType", "ManagedPackVersionID" }) {
            QVERIFY2(!Cli::isWritableSettingsId(QString::fromLatin1(id)), id);
        }

        // the other settings are writable
        for (const auto* id : { "MinMemAlloc", "Env", "JavaPath", "ExportName" }) {
            QVERIFY2(Cli::isWritableSettingsId(QString::fromLatin1(id)), id);
        }
    }

    void test_checkSettingsKeyWritable_rejectsTheManagedKeys()
    {
        const QStringList keys = {
            "uuid",
            "name",
            "notes",
            "instanceType",
            "InstanceType",
            "lastLaunchTime",
            "totalTimePlayed",
            "lastTimePlayed",
            "ManagedPack",
            "ManagedPackType",
        };
        for (const auto& key : keys) {
            const auto error = Cli::checkSettingsKeyWritable(key, Cli::SettingsWriteAction::Set);
            QVERIFY2(error.has_value(), qPrintable(key));
            QCOMPARE(error->code, QStringLiteral("key_not_writable"));
            QVERIFY2(!error->message.isEmpty(), qPrintable(key));

            // the same key cannot be reset either
            const auto unsetError = Cli::checkSettingsKeyWritable(key, Cli::SettingsWriteAction::Unset);
            QVERIFY2(unsetError.has_value(), qPrintable(key));
            QCOMPARE(unsetError->code, QStringLiteral("key_not_writable"));
        }
    }

    void test_checkSettingsKeyWritable_allowsTheOtherKeys()
    {
        const QStringList keys = {
            "memory.max",
            "window.width",
            "java.path",
            "env",
            "env.FOO",
            "export.name",
            "iconKey",
            "nosuchkey",
        };
        for (const auto& key : keys) {
            QVERIFY2(!Cli::checkSettingsKeyWritable(key, Cli::SettingsWriteAction::Set).has_value(), qPrintable(key));
            QVERIFY2(!Cli::checkSettingsKeyWritable(key, Cli::SettingsWriteAction::Unset).has_value(), qPrintable(key));
        }
    }

    void test_checkSettingsKeyWritable_tellsTheAlternative()
    {
        const auto name = Cli::checkSettingsKeyWritable(QStringLiteral("name"), Cli::SettingsWriteAction::Set);
        QVERIFY(name.has_value());
        QVERIFY2(name->message.contains(QStringLiteral("instance rename")), qPrintable(name->message));

        const auto notes = Cli::checkSettingsKeyWritable(QStringLiteral("notes"), Cli::SettingsWriteAction::Set);
        QVERIFY(notes.has_value());
        QVERIFY2(notes->message.contains(QStringLiteral("notes set")), qPrintable(notes->message));
    }

    void test_checkSettingsValue_validatesTheIntegers()
    {
        // the memory of the instance has to be bigger than 16 MB
        QVERIFY(Cli::checkSettingsValue(QStringLiteral("memory.min"), QStringLiteral("16")).has_value());
        QVERIFY(Cli::checkSettingsValue(QStringLiteral("memory.max"), QStringLiteral("16")).has_value());
        QVERIFY(!Cli::checkSettingsValue(QStringLiteral("memory.min"), QStringLiteral("17")).has_value());
        QVERIFY(!Cli::checkSettingsValue(QStringLiteral("memory.max"), QStringLiteral("1024")).has_value());

        // the window of the instance has to be at least a pixel big
        QVERIFY(Cli::checkSettingsValue(QStringLiteral("window.width"), QStringLiteral("0")).has_value());
        QVERIFY(Cli::checkSettingsValue(QStringLiteral("window.height"), QStringLiteral("0")).has_value());
        QVERIFY(!Cli::checkSettingsValue(QStringLiteral("window.width"), QStringLiteral("1")).has_value());
        QVERIFY(!Cli::checkSettingsValue(QStringLiteral("window.height"), QStringLiteral("1080")).has_value());

        // negative values are rejected
        QVERIFY(Cli::checkSettingsValue(QStringLiteral("memory.max"), QStringLiteral("-1")).has_value());
        QVERIFY(Cli::checkSettingsValue(QStringLiteral("window.width"), QStringLiteral("-1")).has_value());
        QVERIFY(Cli::checkSettingsValue(QStringLiteral("memory.perm"), QStringLiteral("-1")).has_value());

        // a value that is not a number is rejected
        const auto notANumber = Cli::checkSettingsValue(QStringLiteral("memory.max"), QStringLiteral("lots"));
        QVERIFY(notANumber.has_value());
        QCOMPARE(notANumber->code, QStringLiteral("invalid_value"));
    }

    void test_checkSettingsValue_validatesTheEnvironment()
    {
        // the environment of the instance is a JSON object
        QVERIFY(Cli::checkSettingsValue(QStringLiteral("env"), QStringLiteral("notjson")).has_value());
        QVERIFY(Cli::checkSettingsValue(QStringLiteral("env"), QStringLiteral("[\"A\"]")).has_value());
        QVERIFY(!Cli::checkSettingsValue(QStringLiteral("env"), QStringLiteral("{\"A\":\"B\"}")).has_value());
        QVERIFY(!Cli::checkSettingsValue(QStringLiteral("env"), QStringLiteral("{}")).has_value());
    }

    void test_checkSettingsValue_validatesTheBooleans()
    {
        QVERIFY(!Cli::checkSettingsValue(QStringLiteral("java.auto"), QStringLiteral("true")).has_value());
        QVERIFY(!Cli::checkSettingsValue(QStringLiteral("java.auto"), QStringLiteral("off")).has_value());
        QVERIFY(Cli::checkSettingsValue(QStringLiteral("java.auto"), QStringLiteral("maybe")).has_value());
    }
};

QTEST_GUILESS_MAIN(SettingsCommandsTest)
#include "SettingsCommands_test.moc"
