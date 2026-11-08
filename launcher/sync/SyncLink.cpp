// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
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

#include "SyncLink.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QUuid>

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace SyncLink {

namespace {
fs::path toPath(const QString& path)
{
#ifdef Q_OS_WIN
    return fs::path(path.toStdWString());
#else
    return fs::path(path.toStdString());
#endif
}

void setError(QString* error, const QString& message)
{
    if (error) {
        *error = message;
    }
}

#ifdef Q_OS_WIN
bool createJunction(const QString& target, const QString& linkPath, QString* error)
{
    // mklink /J creates a directory junction, which unlike a symlink needs no elevation.
    QProcess process;
    process.setProgram("cmd.exe");
    process.setNativeArguments(
        QString("/c mklink /J \"%1\" \"%2\"").arg(QDir::toNativeSeparators(linkPath), QDir::toNativeSeparators(target)));
    process.start();
    if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        setError(error, QObject::tr("Could not create a junction from %1 to %2: %3")
                            .arg(linkPath, target, QString::fromLocal8Bit(process.readAllStandardError()).trimmed()));
        return false;
    }
    return true;
}
#endif
}  // namespace

QString methodName(Method method)
{
    switch (method) {
        case Method::DirectorySymlink:
            return "directory symlink";
        case Method::Junction:
            return "junction";
        case Method::FileSymlink:
            return "file symlink";
        case Method::Copy:
            return "copy";
    }
    return {};
}

Method chooseMethod(bool isDirectory, bool copyOnly, bool fileSymlinksAllowed)
{
    if (copyOnly) {
        return Method::Copy;
    }
    if (isDirectory) {
#ifdef Q_OS_WIN
        return Method::Junction;
#else
        return Method::DirectorySymlink;
#endif
    }
    // Hardlinks are never used: Minecraft and many mods save by writing a new file and renaming it over
    // the old one, which silently breaks a hardlink.
    return fileSymlinksAllowed ? Method::FileSymlink : Method::Copy;
}

bool canCreateFileSymlinks([[maybe_unused]] const QString& probeDir)
{
#ifdef Q_OS_WIN
    static int cached = -1;
    if (cached >= 0) {
        return cached == 1;
    }
    QDir().mkpath(probeDir);
    const auto base = QDir(probeDir).filePath(".symlink-probe-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto target = base + ".target";
    const auto link = base + ".link";
    {
        QFile f(target);
        if (!f.open(QIODevice::WriteOnly)) {
            return false;
        }
    }
    std::error_code ec;
    fs::create_symlink(toPath(target), toPath(link), ec);
    cached = ec ? 0 : 1;
    fs::remove(toPath(link), ec);
    QFile::remove(target);
    return cached == 1;
#else
    return true;
#endif
}

bool createLink(Method method, const QString& target, const QString& linkPath, QString* error)
{
    QDir().mkpath(QFileInfo(linkPath).absolutePath());
    std::error_code ec;
    switch (method) {
        case Method::DirectorySymlink:
            fs::create_directory_symlink(toPath(target), toPath(linkPath), ec);
            break;
        case Method::FileSymlink:
            fs::create_symlink(toPath(target), toPath(linkPath), ec);
            break;
        case Method::Junction:
#ifdef Q_OS_WIN
            return createJunction(target, linkPath, error);
#else
            // Junctions only exist on Windows; a directory symlink is the equivalent elsewhere.
            fs::create_directory_symlink(toPath(target), toPath(linkPath), ec);
            break;
#endif
        case Method::Copy:
            setError(error, QObject::tr("Copy is not a link method"));
            return false;
    }
    if (ec) {
        setError(error, QObject::tr("Could not link %1 to %2: %3").arg(linkPath, target, QString::fromStdString(ec.message())));
        return false;
    }
    return true;
}

bool isLink(const QString& path)
{
    const QFileInfo info(path);
    return info.isSymLink() || info.isJunction();
}

bool isLinkTo(const QString& path, const QString& target)
{
    if (!isLink(path)) {
        return false;
    }
    const auto resolved = QFileInfo(path).canonicalFilePath();
    const auto expected = QFileInfo(target).canonicalFilePath();
    return !resolved.isEmpty() && resolved == expected;
}

bool removeLink(const QString& path)
{
    const QFileInfo info(path);
    if (info.isJunction()) {
        return QDir().rmdir(path);  // removes the junction, not the folder it points at
    }
    if (!info.isSymLink()) {
        return false;
    }
    std::error_code ec;
    return fs::remove(toPath(path), ec) && !ec;
}

}  // namespace SyncLink
