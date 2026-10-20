// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>

#include <optional>
#include <variant>

#include "ApplicationMessage.h"
#include "cli/Commands.h"

namespace {

/// Serialize a command the way the CLI/IPC boundary does, then parse it back.
template <typename T>
std::optional<T> roundTrip(const T& cmd)
{
    const ApplicationMessage message = ApplicationMessage::fromCliCommand(cmd);

    ApplicationMessage decoded;
    const auto parsed = decoded.parse(message.serialize());
    if (!parsed) {
        return std::nullopt;
    }

    const auto command = decoded.toCliCommand();
    if (!command || !std::holds_alternative<T>(*command)) {
        return std::nullopt;
    }
    return std::get<T>(*command);
}

}  // namespace

class ApplicationMessageTest : public QObject {
    Q_OBJECT

   private slots:
    void test_modList()
    {
        const Cli::Cmd::ModList cmd{
            .instance = QStringLiteral("My Instance"),
            .includeDisabled = true,
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_modSearch()
    {
        const Cli::Cmd::ModSearch cmd{
            .query = QStringLiteral("sodium"),
            .mcVersions = { QStringLiteral("1.20.1"), QStringLiteral("1.21") },
            .loaders = { QStringLiteral("fabric"), QStringLiteral("quilt") },
            .limit = 50,
            .offset = 25,
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_modSearch_emptyQueryAndDefaults()
    {
        // An empty query is meaningful ("browse"), so it has to survive the
        // round-trip instead of being dropped.
        const Cli::Cmd::ModSearch cmd{
            .query = QString(),
            .limit = 0,
            .offset = 0,
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_modInfo_optionalInstance()
    {
        const Cli::Cmd::ModInfo withoutInstance{ .project = QStringLiteral("jei") };
        const auto decodedA = roundTrip(withoutInstance);
        QVERIFY(decodedA.has_value());
        QVERIFY(*decodedA == withoutInstance);

        const Cli::Cmd::ModInfo withInstance{
            .project = QStringLiteral("jei"),
            .instance = QStringLiteral("X"),
        };
        const auto decodedB = roundTrip(withInstance);
        QVERIFY(decodedB.has_value());
        QVERIFY(*decodedB == withInstance);
    }

    void test_modInstall_optionalVersionAndFlags()
    {
        const Cli::Cmd::ModInstall withoutVersion{
            .project = QStringLiteral("jei"),
            .instance = QStringLiteral("X"),
        };
        const auto decodedA = roundTrip(withoutVersion);
        QVERIFY(decodedA.has_value());
        QVERIFY(*decodedA == withoutVersion);

        const Cli::Cmd::ModInstall withVersion{
            .project = QStringLiteral("jei"),
            .instance = QStringLiteral("X"),
            .version = QStringLiteral("19.69.0.456"),
            .noDeps = true,
            .noMetadata = true,
            .assumeYes = true,
        };
        const auto decodedB = roundTrip(withVersion);
        QVERIFY(decodedB.has_value());
        QVERIFY(*decodedB == withVersion);

        // Present-but-empty is distinct from absent.
        const Cli::Cmd::ModInstall emptyVersion{
            .project = QStringLiteral("jei"),
            .instance = QStringLiteral("X"),
            .version = QString(),
        };
        const auto decodedC = roundTrip(emptyVersion);
        QVERIFY(decodedC.has_value());
        QVERIFY(*decodedC == emptyVersion);
    }

    void test_modRemove()
    {
        const Cli::Cmd::ModRemove cmd{
            .targets = { QStringLiteral("jei"), QStringLiteral("sodium"), QStringLiteral("fabric-api") },
            .instance = QStringLiteral("X"),
            .force = true,
            .assumeYes = true,
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_modEnableDisable()
    {
        const Cli::Cmd::ModEnable enable{
            .targets = { QStringLiteral("jei") },
            .instance = QStringLiteral("X"),
            .recursive = true,
            .assumeYes = true,
        };
        const auto decodedEnable = roundTrip(enable);
        QVERIFY(decodedEnable.has_value());
        QVERIFY(*decodedEnable == enable);

        const Cli::Cmd::ModDisable disable{
            .targets = { QStringLiteral("jei"), QStringLiteral("sodium") },
            .instance = QStringLiteral("X"),
            .recursive = false,
            .assumeYes = false,
        };
        const auto decodedDisable = roundTrip(disable);
        QVERIFY(decodedDisable.has_value());
        QVERIFY(*decodedDisable == disable);
    }

    void test_modUpdate()
    {
        const Cli::Cmd::ModUpdate all{
            .targets = {},
            .all = true,
            .instance = QStringLiteral("X"),
            .dryRun = true,
            .noDeps = true,
            .force = true,
            .assumeYes = true,
        };
        const auto decodedAll = roundTrip(all);
        QVERIFY(decodedAll.has_value());
        QVERIFY(*decodedAll == all);

        const Cli::Cmd::ModUpdate targets{
            .targets = { QStringLiteral("jei"), QStringLiteral("sodium") },
            .all = false,
            .instance = QStringLiteral("X"),
        };
        const auto decodedTargets = roundTrip(targets);
        QVERIFY(decodedTargets.has_value());
        QVERIFY(*decodedTargets == targets);
    }

    void test_packList()
    {
        for (const auto group : { Cli::Cmd::PackGroup::Resource, Cli::Cmd::PackGroup::Shader, Cli::Cmd::PackGroup::Texture }) {
            const Cli::Cmd::PackList cmd{
                .group = group,
                .instance = QStringLiteral("My Instance"),
                .includeDisabled = true,
            };
            const auto decoded = roundTrip(cmd);
            QVERIFY(decoded.has_value());
            QVERIFY(*decoded == cmd);
        }
    }

    void test_packSearch()
    {
        for (const auto group : { Cli::Cmd::PackGroup::Resource, Cli::Cmd::PackGroup::Shader, Cli::Cmd::PackGroup::Texture }) {
            const Cli::Cmd::PackSearch cmd{
                .group = group,
                .query = QStringLiteral("faithful"),
                .mcVersions = { QStringLiteral("1.20.1"), QStringLiteral("1.21") },
                .limit = 50,
                .offset = 25,
            };
            const auto decoded = roundTrip(cmd);
            QVERIFY(decoded.has_value());
            QVERIFY(*decoded == cmd);
        }
    }

    void test_packInfo_optionalInstance()
    {
        for (const auto group : { Cli::Cmd::PackGroup::Resource, Cli::Cmd::PackGroup::Shader, Cli::Cmd::PackGroup::Texture }) {
            const Cli::Cmd::PackInfo withoutInstance{
                .group = group,
                .project = QStringLiteral("faithful"),
            };
            const auto decodedA = roundTrip(withoutInstance);
            QVERIFY(decodedA.has_value());
            QVERIFY(*decodedA == withoutInstance);

            const Cli::Cmd::PackInfo withInstance{
                .group = group,
                .project = QStringLiteral("faithful"),
                .instance = QStringLiteral("X"),
            };
            const auto decodedB = roundTrip(withInstance);
            QVERIFY(decodedB.has_value());
            QVERIFY(*decodedB == withInstance);
        }
    }

    void test_packInstall_optionalVersionAndFlags()
    {
        for (const auto group : { Cli::Cmd::PackGroup::Resource, Cli::Cmd::PackGroup::Shader, Cli::Cmd::PackGroup::Texture }) {
            const Cli::Cmd::PackInstall withoutVersion{
                .group = group,
                .project = QStringLiteral("faithful"),
                .instance = QStringLiteral("X"),
            };
            const auto decodedA = roundTrip(withoutVersion);
            QVERIFY(decodedA.has_value());
            QVERIFY(*decodedA == withoutVersion);

            const Cli::Cmd::PackInstall withVersion{
                .group = group,
                .project = QStringLiteral("faithful"),
                .instance = QStringLiteral("X"),
                .version = QStringLiteral("32"),
                .noMetadata = true,
                .assumeYes = true,
            };
            const auto decodedB = roundTrip(withVersion);
            QVERIFY(decodedB.has_value());
            QVERIFY(*decodedB == withVersion);
        }
    }

    void test_packRemoveEnableDisable()
    {
        for (const auto group : { Cli::Cmd::PackGroup::Resource, Cli::Cmd::PackGroup::Shader, Cli::Cmd::PackGroup::Texture }) {
            const Cli::Cmd::PackRemove remove{
                .group = group,
                .targets = { QStringLiteral("faithful") },
                .instance = QStringLiteral("X"),
                .force = true,
                .assumeYes = true,
            };
            const auto decodedRemove = roundTrip(remove);
            QVERIFY(decodedRemove.has_value());
            QVERIFY(*decodedRemove == remove);

            const Cli::Cmd::PackEnable enable{
                .group = group,
                .targets = { QStringLiteral("faithful") },
                .instance = QStringLiteral("X"),
            };
            const auto decodedEnable = roundTrip(enable);
            QVERIFY(decodedEnable.has_value());
            QVERIFY(*decodedEnable == enable);

            const Cli::Cmd::PackDisable disable{
                .group = group,
                .targets = { QStringLiteral("faithful") },
                .instance = QStringLiteral("X"),
            };
            const auto decodedDisable = roundTrip(disable);
            QVERIFY(decodedDisable.has_value());
            QVERIFY(*decodedDisable == disable);
        }
    }

    void test_worldList()
    {
        const Cli::Cmd::WorldList cmd{
            .instance = QStringLiteral("My Instance"),
            .withSize = true,
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_worldInfo()
    {
        const Cli::Cmd::WorldInfo cmd{
            .instance = QStringLiteral("My Instance"),
            .target = QStringLiteral("New World"),
            .withSize = true,
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_worldCopy()
    {
        const Cli::Cmd::WorldCopy withoutName{
            .instance = QStringLiteral("My Instance"),
            .target = QStringLiteral("New World"),
        };
        const auto decoded = roundTrip(withoutName);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == withoutName);

        const Cli::Cmd::WorldCopy withName{
            .instance = QStringLiteral("My Instance"),
            .target = QStringLiteral("New World"),
            .name = QStringLiteral("Copy of the world"),
        };
        const auto decodedNamed = roundTrip(withName);
        QVERIFY(decodedNamed.has_value());
        QVERIFY(*decodedNamed == withName);
    }

    void test_worldRename()
    {
        const Cli::Cmd::WorldRename cmd{
            .instance = QStringLiteral("My Instance"),
            .target = QStringLiteral("New World"),
            .name = QStringLiteral("Renamed"),
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_worldDelete()
    {
        const Cli::Cmd::WorldDelete cmd{
            .instance = QStringLiteral("My Instance"),
            .target = QStringLiteral("New World"),
            .permanent = true,
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_worldImport()
    {
        const Cli::Cmd::WorldImport withoutName{
            .instance = QStringLiteral("My Instance"),
            .path = QStringLiteral("/tmp/world.zip"),
        };
        const auto decoded = roundTrip(withoutName);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == withoutName);

        const Cli::Cmd::WorldImport withName{
            .instance = QStringLiteral("My Instance"),
            .path = QStringLiteral("/tmp/world.zip"),
            .name = QStringLiteral("Imported"),
        };
        const auto decodedNamed = roundTrip(withName);
        QVERIFY(decodedNamed.has_value());
        QVERIFY(*decodedNamed == withName);
    }

    void test_serverList()
    {
        const Cli::Cmd::ServerList cmd{
            .instance = QStringLiteral("My Instance"),
            .ping = true,
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_serverAdd()
    {
        const Cli::Cmd::ServerAdd withoutIndex{
            .instance = QStringLiteral("My Instance"),
            .name = QStringLiteral("Hypixel"),
            .address = QStringLiteral("mc.hypixel.net"),
            .acceptsTextures = Cli::Cmd::AcceptsTextures::Never,
        };
        const auto decoded = roundTrip(withoutIndex);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == withoutIndex);

        const Cli::Cmd::ServerAdd withIndex{
            .instance = QStringLiteral("My Instance"),
            .name = QStringLiteral("Hypixel"),
            .address = QStringLiteral("mc.hypixel.net"),
            .index = 3,
            .acceptsTextures = Cli::Cmd::AcceptsTextures::Always,
        };
        const auto decodedIndexed = roundTrip(withIndex);
        QVERIFY(decodedIndexed.has_value());
        QVERIFY(*decodedIndexed == withIndex);
    }

    void test_serverRemove()
    {
        const Cli::Cmd::ServerRemove cmd{
            .instance = QStringLiteral("My Instance"),
            .target = QStringLiteral("0"),
            .force = true,
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_logList()
    {
        const Cli::Cmd::LogList cmd{
            .instance = QStringLiteral("My Instance"),
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_logRead()
    {
        const Cli::Cmd::LogRead cmd{
            .instance = QStringLiteral("My Instance"),
            .file = QStringLiteral("logs/latest.log"),
            .head = 10,
            .grep = QStringLiteral("ERROR"),
            .level = QStringLiteral("warning"),
            .json = true,
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_logUpload()
    {
        const Cli::Cmd::LogUpload cmd{
            .instance = QStringLiteral("My Instance"),
            .file = QStringLiteral("logs/latest.log"),
            .service = QStringLiteral("mclogs"),
            .anonymize = true,
            .assumeYes = true,
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_targetList_newlineIsASeparator()
    {
        // Known limitation of the IPC encoding: target lists are joined with
        // '\n', so a target containing a newline cannot be represented. Keep
        // the behavior pinned so a change is intentional.
        const Cli::Cmd::ModRemove cmd{
            .targets = { QStringLiteral("a\nb") },
            .instance = QStringLiteral("X"),
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->targets.size(), std::size_t{ 2 });
        QCOMPARE(decoded->targets.at(0), QStringLiteral("a"));
        QCOMPARE(decoded->targets.at(1), QStringLiteral("b"));
    }
};

QTEST_GUILESS_MAIN(ApplicationMessageTest)
#include "ApplicationMessage_test.moc"
