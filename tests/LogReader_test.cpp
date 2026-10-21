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

#include <GZip.h>
#include <FileSystem.h>
#include <logs/LogReader.h>

namespace {

/// The content of a log with the given number of lines.
QByteArray logContent(int lines, bool trailingNewline = true)
{
    QByteArray content;
    for (int i = 1; i <= lines; ++i) {
        content += "line " + QByteArray::number(i) + "\n";
    }
    if (!trailingNewline) {
        content.chop(1);
    }
    return content;
}

/// Write a file and return its path.
QString writeFile(const QString& path, const QByteArray& content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(content);
    file.close();
    return path;
}

/// Write a gzipped file and return its path.
QString writeGzFile(const QString& path, const QByteArray& content)
{
    QByteArray compressed;
    if (!GZip::zip(content, compressed)) {
        return {};
    }
    return writeFile(path, compressed);
}

/// The content of a log file - the test fails when the reading did not work.
LogReader::Content readLog(const QString& path, int head, int tail)
{
    const auto result = LogReader::read(path, head, tail);
    if (!result) {
        qWarning() << "the log could not be read:" << result.error();
        return {};
    }
    return result.value();
}

}  // namespace

class LogReaderTest : public QObject {
    Q_OBJECT

   private slots:
    void test_readAll()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = writeFile(FS::PathCombine(dir.path(), "latest.log"), logContent(5));
        QVERIFY(!path.isEmpty());

        const auto content = readLog(path, 0, 0);
        QCOMPARE(content.lines.size(), 5);
        QCOMPARE(content.truncated, false);
        QCOMPARE(content.lines.at(0).number, 1);
        QCOMPARE(content.lines.at(0).text, QString("line 1"));
        QCOMPARE(content.lines.at(4).number, 5);
        QCOMPARE(content.lines.at(4).text, QString("line 5"));
    }

    void test_readWithoutTrailingNewline()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = writeFile(FS::PathCombine(dir.path(), "latest.log"), logContent(3, /*trailingNewline=*/false));
        QVERIFY(!path.isEmpty());

        const auto content = readLog(path, 0, 0);
        QCOMPARE(content.lines.size(), 3);
        QCOMPARE(content.lines.at(2).text, QString("line 3"));
    }

    void test_readHead()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = writeFile(FS::PathCombine(dir.path(), "latest.log"), logContent(10));
        QVERIFY(!path.isEmpty());

        const auto content = readLog(path, 3, 0);
        QCOMPARE(content.lines.size(), 3);
        QCOMPARE(content.truncated, true);
        QCOMPARE(content.lines.at(0).number, 1);
        QCOMPARE(content.lines.at(2).number, 3);
    }

    void test_readTail()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = writeFile(FS::PathCombine(dir.path(), "latest.log"), logContent(10));
        QVERIFY(!path.isEmpty());

        const auto content = readLog(path, 0, 3);
        QCOMPARE(content.lines.size(), 3);
        QCOMPARE(content.truncated, true);
        // the lines are numbered as they are in the file
        QCOMPARE(content.lines.at(0).number, 8);
        QCOMPARE(content.lines.at(0).text, QString("line 8"));
        QCOMPARE(content.lines.at(2).number, 10);
    }

    void test_readTailOfAFileWithoutTrailingNewline()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = writeFile(FS::PathCombine(dir.path(), "latest.log"), logContent(4, /*trailingNewline=*/false));
        QVERIFY(!path.isEmpty());

        const auto content = readLog(path, 0, 2);
        QCOMPARE(content.lines.size(), 2);
        QCOMPARE(content.lines.at(0).number, 3);
        QCOMPARE(content.lines.at(1).number, 4);
        QCOMPARE(content.lines.at(1).text, QString("line 4"));
    }

    void test_readTailOfABigFile()
    {
        // The file is bigger than the block it is read in, so the tail is
        // read from more than one block.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = writeFile(FS::PathCombine(dir.path(), "latest.log"), logContent(20000));
        QVERIFY(!path.isEmpty());

        const auto content = readLog(path, 0, 5);
        QCOMPARE(content.lines.size(), 5);
        QCOMPARE(content.truncated, true);
        QCOMPARE(content.lines.at(0).number, 19996);
        QCOMPARE(content.lines.at(4).number, 20000);
        QCOMPARE(content.lines.at(4).text, QString("line 20000"));
    }

    void test_readGzipped()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = writeGzFile(FS::PathCombine(dir.path(), "old.log.gz"), logContent(6));
        QVERIFY(!path.isEmpty());

        const auto content = readLog(path, 0, 0);
        QCOMPARE(content.lines.size(), 6);
        QCOMPARE(content.truncated, false);
        QCOMPARE(content.lines.at(0).text, QString("line 1"));
        QCOMPARE(content.lines.at(5).number, 6);
    }

    void test_readTailOfAGzippedFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = writeGzFile(FS::PathCombine(dir.path(), "old.log.gz"), logContent(6, /*trailingNewline=*/false));
        QVERIFY(!path.isEmpty());

        const auto content = readLog(path, 0, 2);
        QCOMPARE(content.lines.size(), 2);
        QCOMPARE(content.truncated, true);
        QCOMPARE(content.lines.at(0).number, 5);
        QCOMPARE(content.lines.at(1).number, 6);
        QCOMPARE(content.lines.at(1).text, QString("line 6"));
    }

    void test_readHeadOfAGzippedFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = writeGzFile(FS::PathCombine(dir.path(), "old.log.gz"), logContent(6));
        QVERIFY(!path.isEmpty());

        const auto content = readLog(path, 2, 0);
        QCOMPARE(content.lines.size(), 2);
        QCOMPARE(content.truncated, true);
        QCOMPARE(content.lines.at(1).text, QString("line 2"));
    }

    void test_readAMissingFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // A file that cannot be opened is a failure - it is not an empty log.
        const auto result = LogReader::read(FS::PathCombine(dir.path(), "nope.log"), 0, 0);
        QVERIFY(!result.has_value());
        QVERIFY(!result.error().isEmpty());
    }

    void test_readABrokenGzippedFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // A file that cannot be decompressed is a failure too.
        const auto path = writeFile(FS::PathCombine(dir.path(), "broken.log.gz"), QByteArray("not a gzip file"));
        QVERIFY(!path.isEmpty());

        const auto result = LogReader::read(path, 0, 0);
        QVERIFY(!result.has_value());
        QVERIFY(result.error().contains("decompressed"));
    }
};

QTEST_GUILESS_MAIN(LogReaderTest)

#include "LogReader_test.moc"
