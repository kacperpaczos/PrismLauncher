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

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <sstream>

#include <GZip.h>
#include <FileSystem.h>
#include <minecraft/World.h>

#include <io/stream_writer.h>
#include <tag_compound.h>
#include <value_initializer.h>

namespace {

/**
 * Write a `level.dat` of a world.
 *
 * The file is the gzipped NBT of a compound with a `Data` compound in it -
 * the same layout the game uses.
 */
bool writeLevelDat(const QString& path, const QString& name, int gameType, qint64 lastPlayed, qint64 seed)
{
    nbt::tag_compound data;
    data.put("LevelName", nbt::value_initializer(name.toUtf8().constData()));
    data.put("GameType", nbt::value_initializer(static_cast<int32_t>(gameType)));
    data.put("LastPlayed", nbt::value_initializer(static_cast<int64_t>(lastPlayed)));
    data.put("RandomSeed", nbt::value_initializer(static_cast<int64_t>(seed)));

    nbt::tag_compound root;
    root.insert("Data", nbt::value(std::move(data)));

    std::ostringstream stream;
    try {
        nbt::io::write_tag("", root, stream);
    } catch (const std::exception&) {
        return false;
    }
    const std::string written = stream.str();

    QByteArray compressed;
    if (!GZip::zip(QByteArray(written.data(), static_cast<int>(written.size())), compressed)) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(compressed) == compressed.size();
}

}  // namespace

class WorldTest : public QObject {
    Q_OBJECT

   private slots:
    void test_metadata()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto worldDir = FS::PathCombine(dir.path(), "New World");
        QVERIFY(FS::ensureFolderPathExists(worldDir));
        QVERIFY(writeLevelDat(FS::PathCombine(worldDir, "level.dat"), "Test World", 1, 1700000000000, 123456789));

        World world{ QFileInfo(worldDir) };
        QVERIFY(world.isValid());
        QCOMPARE(world.folderName(), QString("New World"));

        world.loadMetadata();
        QCOMPARE(world.name(), QString("Test World"));
        QCOMPARE(world.gameType().type, GameType::Creative);
        QCOMPARE(world.gameType().original.value(), 1);
        QCOMPARE(world.seed(), 123456789);
        QCOMPARE(world.lastPlayed().toMSecsSinceEpoch(), 1700000000000);
    }

    void test_aFolderWithoutALevelDatIsNotAWorld()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto worldDir = FS::PathCombine(dir.path(), "Empty");
        QVERIFY(FS::ensureFolderPathExists(worldDir));

        World world{ QFileInfo(worldDir) };
        QVERIFY(!world.isValid());
    }

    void test_aBrokenLevelDatIsNotAWorld()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto worldDir = FS::PathCombine(dir.path(), "Broken");
        QVERIFY(FS::ensureFolderPathExists(worldDir));

        QFile file(FS::PathCombine(worldDir, "level.dat"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is not nbt at all");
        file.close();

        World world{ QFileInfo(worldDir) };
        QVERIFY(world.isValid());
        world.loadMetadata();
        // the metadata could not be read - the world has no name
        QVERIFY(world.name().isEmpty());
    }
};

QTEST_GUILESS_MAIN(WorldTest)

#include "World_test.moc"
