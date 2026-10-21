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

#include <FileSystem.h>

#include <minecraft/ServersDat.h>

#include <io/stream_reader.h>
#include <io/stream_writer.h>
#include <tag_compound.h>
#include <tag_list.h>
#include <tag_string.h>

class ServersDatTest : public QObject {
    Q_OBJECT

   private slots:
    void test_missingFileIsEmpty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        ServersDat dat(FS::PathCombine(dir.path(), "servers.dat"));
        QCOMPARE(dat.load(), ServersDat::State::Missing);
        QVERIFY(dat.servers().isEmpty());
        QVERIFY(dat.isLoaded());
    }

    void test_writeAndRead()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = FS::PathCombine(dir.path(), "servers.dat");

        {
            ServersDat dat(path);
            dat.add(Server("Hypixel", "mc.hypixel.net"));
            dat.add(Server("Second", "example.com:25565"));
            dat.servers()[1].m_acceptsTextures = Server::AcceptsTextures::NEVER;
            dat.servers()[1].m_icon = QByteArray::fromBase64("aW1hZ2U=");
            QVERIFY(dat.save());
        }

        ServersDat dat(path);
        QCOMPARE(dat.load(), ServersDat::State::Loaded);
        QCOMPARE(dat.servers().size(), 2);

        QCOMPARE(dat.servers()[0].m_name, QString("Hypixel"));
        QCOMPARE(dat.servers()[0].m_address, QString("mc.hypixel.net"));
        QCOMPARE(dat.servers()[0].m_acceptsTextures, Server::AcceptsTextures::ASK);
        QVERIFY(dat.servers()[0].m_icon.isEmpty());

        QCOMPARE(dat.servers()[1].m_name, QString("Second"));
        QCOMPARE(dat.servers()[1].m_address, QString("example.com:25565"));
        QCOMPARE(dat.servers()[1].m_acceptsTextures, Server::AcceptsTextures::NEVER);
        QCOMPARE(dat.servers()[1].m_icon, QByteArray::fromBase64("aW1hZ2U="));
    }

    void test_saveCreatesTheFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = FS::PathCombine(dir.path(), "servers.dat");
        QVERIFY(!QFile::exists(path));

        ServersDat dat(path);
        dat.add(Server("New", "example.com"));
        QVERIFY(dat.save());
        QVERIFY(QFile::exists(path));

        ServersDat read(path);
        QCOMPARE(read.load(), ServersDat::State::Loaded);
        QCOMPARE(read.servers().size(), 1);
        QCOMPARE(read.servers()[0].m_name, QString("New"));
    }

    void test_addAndRemove()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        ServersDat dat(FS::PathCombine(dir.path(), "servers.dat"));
        dat.add(Server("First", "one.example"));
        dat.add(Server("Second", "two.example"));
        // an index outside of the list appends the server
        dat.add(Server("Third", "three.example"), 42);
        dat.add(Server("Zero", "zero.example"), 0);
        QCOMPARE(dat.servers().size(), 4);
        QCOMPARE(dat.servers()[0].m_name, QString("Zero"));

        QVERIFY(dat.removeAt(0));
        QCOMPARE(dat.servers().size(), 3);
        QCOMPARE(dat.servers()[0].m_name, QString("First"));

        QVERIFY(!dat.removeAt(-1));
        QVERIFY(!dat.removeAt(3));
    }

    void test_corruptedFileIsNotOverwritten()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = FS::PathCombine(dir.path(), "servers.dat");

        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is not nbt at all");
        file.close();

        ServersDat dat(path);
        QCOMPARE(dat.load(), ServersDat::State::Corrupted);
        QVERIFY(!dat.isLoaded());
        QVERIFY(dat.servers().isEmpty());

        // a corrupted file is never overwritten
        dat.add(Server("Should not be written", "example.com"));
        QVERIFY(!dat.save());

        QFile check(path);
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), QByteArray("this is not nbt at all"));
    }

    void test_backupIsWritten()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = FS::PathCombine(dir.path(), "servers.dat");

        {
            ServersDat dat(path);
            dat.add(Server("First", "one.example"));
            QVERIFY(dat.save());
        }
        {
            ServersDat dat(path);
            QCOMPARE(dat.load(), ServersDat::State::Loaded);
            dat.add(Server("Second", "two.example"));
            QVERIFY(dat.save());
        }

        QVERIFY(QFile::exists(path + ".bak"));
        ServersDat backup(path + ".bak");
        QCOMPARE(backup.load(), ServersDat::State::Loaded);
        QCOMPARE(backup.servers().size(), 1);
        QCOMPARE(backup.servers()[0].m_name, QString("First"));
    }

    void test_unknownTagsSurviveAnEdit()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = FS::PathCombine(dir.path(), "servers.dat");

        // A file with a tag that the CLI does not know.
        {
            nbt::tag_compound root;
            nbt::tag_list servers;
            nbt::tag_compound server;
            server.insert("name", std::string("Hypixel"));
            server.insert("ip", std::string("mc.hypixel.net"));
            servers.push_back(std::move(server));
            root.insert("servers", nbt::value(std::move(servers)));
            root.insert("someUnknownTag", std::string("a value"));

            std::ostringstream stream;
            nbt::io::write_tag("", root, stream);
            const auto data = stream.str();

            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(data.data(), static_cast<int>(data.size()));
            file.close();
        }

        // The list is edited...
        {
            ServersDat dat(path);
            QCOMPARE(dat.load(), ServersDat::State::Loaded);
            QCOMPARE(dat.servers().size(), 1);
            dat.add(Server("Second", "example.com:25565"));
            QVERIFY(dat.save());
        }

        // ...and the unknown tag is still there.
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto data = file.readAll();
            file.close();

            std::istringstream stream(std::string(data.constData(), data.size()));
            auto pair = nbt::io::read_compound(stream);
            QVERIFY(pair.second != nullptr);
            QVERIFY(pair.second->has_key("someUnknownTag"));
            QCOMPARE(pair.second->at("someUnknownTag").as<nbt::tag_string>().get(), std::string("a value"));
        }

        // The list itself was changed.
        ServersDat dat(path);
        QCOMPARE(dat.load(), ServersDat::State::Loaded);
        QCOMPARE(dat.servers().size(), 2);
        QCOMPARE(dat.servers()[1].m_name, QString("Second"));
    }
};

QTEST_GUILESS_MAIN(ServersDatTest)

#include "ServersDat_test.moc"
