// SPDX-FileCopyrightText: 2026 Rachel Powers <508861+Ryex@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>

#include <optional>
#include <variant>

#include "ApplicationMessage.h"
#include "cli/Commands.h"
#include "cli/Output.h"

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

    void test_instanceListAndInfo()
    {
        const Cli::Cmd::InstanceList list{ .group = QStringLiteral("modded") };
        const auto decodedList = roundTrip(list);
        QVERIFY(decodedList.has_value());
        QVERIFY(*decodedList == list);

        const Cli::Cmd::InstanceList noGroup{};
        const auto decodedNoGroup = roundTrip(noGroup);
        QVERIFY(decodedNoGroup.has_value());
        QVERIFY(*decodedNoGroup == noGroup);

        const Cli::Cmd::InstanceInfo info{ .id = QStringLiteral("My Instance") };
        const auto decodedInfo = roundTrip(info);
        QVERIFY(decodedInfo.has_value());
        QVERIFY(*decodedInfo == info);
    }

    void test_instanceCreate()
    {
        const Cli::Cmd::InstanceCreate cmd{
            .name = QStringLiteral("Test"),
            .mcVersion = QStringLiteral("1.21.1"),
            .loader = QStringLiteral("fabric"),
            .loaderVersion = QStringLiteral("latest"),
            .group = QStringLiteral("modded"),
            .icon = QStringLiteral("default"),
            .download = true,
            .from = std::nullopt,
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_instanceCreate_import()
    {
        const Cli::Cmd::InstanceCreate cmd{
            .name = QString(),
            .mcVersion = QString(),
            .loader = std::nullopt,
            .loaderVersion = std::nullopt,
            .group = std::nullopt,
            .icon = std::nullopt,
            .download = false,
            .from = QStringLiteral("https://example.com/pack.mrpack"),
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_instanceDuplicateRenameDeleteIcon()
    {
        const Cli::Cmd::InstanceDuplicate duplicate{
            .id = QStringLiteral("X"),
            .name = QStringLiteral("Y"),
            .copyMode = QStringLiteral("hardlinks"),
            .noSaves = true,
            .noMods = false,
            .noResourcePacks = true,
            .noShaderPacks = false,
            .noOptions = true,
            .noConfigs = false,
        };
        const auto decodedDuplicate = roundTrip(duplicate);
        QVERIFY(decodedDuplicate.has_value());
        QVERIFY(*decodedDuplicate == duplicate);

        const Cli::Cmd::InstanceRename rename{
            .id = QStringLiteral("X"),
            .newName = QStringLiteral("New Name"),
            .renameFolder = true,
        };
        const auto decodedRename = roundTrip(rename);
        QVERIFY(decodedRename.has_value());
        QVERIFY(*decodedRename == rename);

        const Cli::Cmd::InstanceDelete remove{
            .id = QStringLiteral("X"),
            .purge = true,
        };
        const auto decodedRemove = roundTrip(remove);
        QVERIFY(decodedRemove.has_value());
        QVERIFY(*decodedRemove == remove);

        const Cli::Cmd::InstanceIconSet icon{
            .id = QStringLiteral("X"),
            .icon = QStringLiteral("default"),
        };
        const auto decodedIcon = roundTrip(icon);
        QVERIFY(decodedIcon.has_value());
        QVERIFY(*decodedIcon == icon);
    }

    void test_settings()
    {
        const Cli::Cmd::SettingsList list{
            .id = QStringLiteral("X"),
            .all = true,
            .modified = true,
        };
        const auto decodedList = roundTrip(list);
        QVERIFY(decodedList.has_value());
        QVERIFY(*decodedList == list);

        const Cli::Cmd::SettingsGet get{
            .id = QStringLiteral("X"),
            .keys = { QStringLiteral("memory.max"), QStringLiteral("memory.min") },
        };
        const auto decodedGet = roundTrip(get);
        QVERIFY(decodedGet.has_value());
        QVERIFY(*decodedGet == get);

        const Cli::Cmd::SettingsSet set{
            .id = QStringLiteral("X"),
            .values = { { QStringLiteral("memory.max"), QStringLiteral("4096") },
                        { QStringLiteral("env.FOO"), QStringLiteral("bar=baz") } },
        };
        const auto decodedSet = roundTrip(set);
        QVERIFY(decodedSet.has_value());
        QVERIFY(*decodedSet == set);

        const Cli::Cmd::SettingsUnset unset{
            .id = QStringLiteral("X"),
            .keys = { QStringLiteral("memory.max") },
        };
        const auto decodedUnset = roundTrip(unset);
        QVERIFY(decodedUnset.has_value());
        QVERIFY(*decodedUnset == unset);
    }

    void test_version()
    {
        const Cli::Cmd::VersionShow show{ .id = QStringLiteral("X") };
        const auto decodedShow = roundTrip(show);
        QVERIFY(decodedShow.has_value());
        QVERIFY(*decodedShow == show);

        const Cli::Cmd::VersionListAvailable list{
            .loader = QStringLiteral("fabric"),
            .mcVersion = QStringLiteral("1.21.1"),
            .type = QStringLiteral("release"),
        };
        const auto decodedList = roundTrip(list);
        QVERIFY(decodedList.has_value());
        QVERIFY(*decodedList == list);

        const Cli::Cmd::VersionListAvailable minecraft{
            .loader = std::nullopt,
            .mcVersion = std::nullopt,
            .type = QStringLiteral("all"),
        };
        const auto decodedMinecraft = roundTrip(minecraft);
        QVERIFY(decodedMinecraft.has_value());
        QVERIFY(*decodedMinecraft == minecraft);

        const Cli::Cmd::VersionChangeMc change{ .id = QStringLiteral("X"), .version = QStringLiteral("1.21.1") };
        const auto decodedChange = roundTrip(change);
        QVERIFY(decodedChange.has_value());
        QVERIFY(*decodedChange == change);

        const Cli::Cmd::VersionInstallLoader install{
            .id = QStringLiteral("X"),
            .loader = QStringLiteral("fabric"),
            .version = QStringLiteral("latest"),
            .forceConflict = QStringLiteral("replace"),
        };
        const auto decodedInstall = roundTrip(install);
        QVERIFY(decodedInstall.has_value());
        QVERIFY(*decodedInstall == install);

        const Cli::Cmd::VersionRemoveLoader remove{ .id = QStringLiteral("X"), .loader = QStringLiteral("quilt") };
        const auto decodedRemove = roundTrip(remove);
        QVERIFY(decodedRemove.has_value());
        QVERIFY(*decodedRemove == remove);

        const Cli::Cmd::VersionReinstall reinstall{ .id = QStringLiteral("X") };
        const auto decodedReinstall = roundTrip(reinstall);
        QVERIFY(decodedReinstall.has_value());
        QVERIFY(*decodedReinstall == reinstall);
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

    void test_screenshotList()
    {
        const Cli::Cmd::ScreenshotList cmd{
            .instance = QStringLiteral("My Instance"),
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_screenshotUpload()
    {
        const Cli::Cmd::ScreenshotUpload withoutLimit{
            .instance = QStringLiteral("My Instance"),
            .target = QStringLiteral("all"),
            .assumeYes = true,
        };
        const auto decoded = roundTrip(withoutLimit);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == withoutLimit);

        const Cli::Cmd::ScreenshotUpload withLimit{
            .instance = QStringLiteral("My Instance"),
            .target = QStringLiteral("shot.png"),
            .limit = 5,
            .assumeYes = true,
        };
        const auto decodedLimited = roundTrip(withLimit);
        QVERIFY(decodedLimited.has_value());
        QVERIFY(*decodedLimited == withLimit);
    }

    void test_screenshotCopy()
    {
        const Cli::Cmd::ScreenshotCopy cmd{
            .instance = QStringLiteral("My Instance"),
            .id = QStringLiteral("shot.png"),
            .destination = QStringLiteral("/tmp/shot.png"),
            .force = true,
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_notesShow()
    {
        const Cli::Cmd::NotesShow cmd{
            .instance = QStringLiteral("My Instance"),
        };
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_notesSet()
    {
        const Cli::Cmd::NotesSet withoutText{
            .instance = QStringLiteral("My Instance"),
            .append = true,
        };
        const auto decoded = roundTrip(withoutText);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == withoutText);

        const Cli::Cmd::NotesSet withText{
            .instance = QStringLiteral("My Instance"),
            .text = QStringLiteral("some notes"),
            .file = QStringLiteral("/tmp/notes.txt"),
            .append = false,
        };
        const auto decodedText = roundTrip(withText);
        QVERIFY(decodedText.has_value());
        QVERIFY(*decodedText == withText);
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

    void test_status()
    {
        const Cli::Cmd::Status cmd{};
        const auto decoded = roundTrip(cmd);
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == cmd);
    }

    void test_request_carriesTheRequestId()
    {
        const Cli::Cmd::ModList cmd{
            .instance = QStringLiteral("My Instance"),
            .includeDisabled = true,
        };
        const auto message = ApplicationMessage::request(cmd, QStringLiteral("the-request-id"));
        QVERIFY(message.isRequest());
        QVERIFY(message.expectsReply());
        QCOMPARE(message.requestId, QStringLiteral("the-request-id"));

        ApplicationMessage decoded;
        QVERIFY(decoded.parse(message.serialize()));
        QVERIFY(decoded.isRequest());
        QVERIFY(decoded.expectsReply());
        QCOMPARE(decoded.requestId, QStringLiteral("the-request-id"));
        QCOMPARE(decoded.command, QStringLiteral("mod_list"));

        const auto command = decoded.toCliCommand();
        QVERIFY(command);
        QVERIFY(std::holds_alternative<Cli::Cmd::ModList>(*command));
        QVERIFY(std::get<Cli::Cmd::ModList>(*command) == cmd);
    }

    void test_legacyMessageIsNotAnswered()
    {
        // A message without a request id is a message of the first version of
        // the protocol - it is not answered. Its format has to stay the same,
        // so that an old launcher can handle it.
        const Cli::Cmd::ModList cmd{
            .instance = QStringLiteral("My Instance"),
            .includeDisabled = true,
        };
        const auto message = ApplicationMessage::fromCliCommand(cmd);
        QVERIFY(!message.expectsReply());

        const auto document = QJsonDocument::fromJson(message.serialize());
        QVERIFY(document.isObject());
        QVERIFY(!document.object().contains("type"));
        QVERIFY(!document.object().contains("requestId"));
        QVERIFY(document.object().contains("command"));
        QVERIFY(document.object().contains("args"));
    }

    void test_acceptedRoundTrip()
    {
        const auto message = ApplicationMessage::accepted(QStringLiteral("the-request-id"));

        ApplicationMessage decoded;
        QVERIFY(decoded.parse(message.serialize()));
        QCOMPARE(decoded.type, ApplicationMessageType::Accepted);
        QCOMPARE(decoded.requestId, QStringLiteral("the-request-id"));
        QVERIFY(!decoded.expectsReply());
    }

    void test_responseRoundTrip()
    {
        QJsonObject data;
        data.insert("ok", true);
        data.insert("total", 3);

        const auto message = ApplicationMessage::response(QStringLiteral("the-request-id"), true, 0, data);
        QVERIFY(message.isResponse());

        ApplicationMessage decoded;
        QVERIFY(decoded.parse(message.serialize()));
        QVERIFY(decoded.isResponse());
        QCOMPARE(decoded.requestId, QStringLiteral("the-request-id"));
        QCOMPARE(decoded.ok, true);
        QCOMPARE(decoded.exitCode, 0);
        QCOMPARE(decoded.data, data);
        QVERIFY(decoded.errorCode.isEmpty());
    }

    void test_responseCarriesTheError()
    {
        const auto envelope = Cli::errorEnvelope(QStringLiteral("instance_not_found"), QStringLiteral("no such instance"));
        const auto message = ApplicationMessage::response(QStringLiteral("the-request-id"), false, 3, envelope);
        QCOMPARE(message.ok, false);
        QCOMPARE(message.exitCode, 3);
        QCOMPARE(message.errorCode, QStringLiteral("instance_not_found"));
        QCOMPARE(message.errorMessage, QStringLiteral("no such instance"));

        ApplicationMessage decoded;
        QVERIFY(decoded.parse(message.serialize()));
        QCOMPARE(decoded.ok, false);
        QCOMPARE(decoded.exitCode, 3);
        QCOMPARE(decoded.errorCode, QStringLiteral("instance_not_found"));
        QCOMPARE(decoded.errorMessage, QStringLiteral("no such instance"));
        QCOMPARE(decoded.data, envelope);
    }

    void test_responseOfARawOutput()
    {
        auto message = ApplicationMessage::response(QStringLiteral("the-request-id"), true, 0, QJsonObject{});
        message.rawOutput = true;

        ApplicationMessage decoded;
        QVERIFY(decoded.parse(message.serialize()));
        QVERIFY(decoded.rawOutput);
    }
};

QTEST_GUILESS_MAIN(ApplicationMessageTest)
#include "ApplicationMessage_test.moc"
