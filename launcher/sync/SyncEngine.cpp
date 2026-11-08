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

#include "SyncEngine.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSaveFile>

namespace {
void setError(QString* error, const QString& message)
{
    if (error) {
        *error = message;
    }
}

bool copyFileNewer(const QString& src, const QString& dst, QString* error)
{
    const QFileInfo srcInfo(src);
    const QFileInfo dstInfo(dst);
    if (dstInfo.exists() && !dstInfo.isSymLink() && dstInfo.lastModified() >= srcInfo.lastModified()) {
        return true;
    }
    QDir().mkpath(dstInfo.absolutePath());
    if (dstInfo.exists() || dstInfo.isSymLink()) {
        QFile::remove(dst);
    }
    if (!QFile::copy(src, dst)) {
        setError(error, QObject::tr("Could not copy %1 to %2").arg(src, dst));
        return false;
    }
    // Keep the source's time so the next sync in the other direction sees the files as equal.
    QFile copied(dst);
    if (copied.open(QIODevice::ReadWrite)) {
        copied.setFileTime(srcInfo.lastModified(), QFileDevice::FileModificationTime);
    }
    return true;
}
}  // namespace

namespace SyncFiles {

bool copyNewer(const QString& src, const QString& dst, QString* error)
{
    const QFileInfo srcInfo(src);
    if (!srcInfo.exists()) {
        setError(error, QObject::tr("%1 does not exist").arg(src));
        return false;
    }
    if (!srcInfo.isDir()) {
        return copyFileNewer(src, dst, error);
    }
    if (!QDir().mkpath(dst)) {
        setError(error, QObject::tr("Could not create %1").arg(dst));
        return false;
    }
    const QDir srcDir(src);
    QDirIterator it(src, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto file = it.next();
        if (!copyFileNewer(file, QDir(dst).filePath(srcDir.relativeFilePath(file)), error)) {
            return false;
        }
    }
    return true;
}

bool movePath(const QString& src, const QString& dst, QString* error)
{
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (QFile::rename(src, dst)) {
        return true;
    }
    // Different file systems: copy, then delete the original.
    if (!copyNewer(src, dst, error)) {
        return false;
    }
    const bool removed = QFileInfo(src).isDir() ? QDir(src).removeRecursively() : QFile::remove(src);
    if (!removed) {
        setError(error, QObject::tr("Copied %1 to %2 but could not remove the original").arg(src, dst));
        return false;
    }
    return true;
}

QString backupPathFor(const QString& path)
{
    const auto base = path + ".sync-backup-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    auto candidate = base;
    for (int i = 2; QFileInfo::exists(candidate) || QFileInfo(candidate).isSymLink(); i++) {
        candidate = QString("%1-%2").arg(base).arg(i);
    }
    return candidate;
}

}  // namespace SyncFiles

SyncEngine::SyncEngine(SyncProfile profile, QString gameRoot) : m_profile(std::move(profile)), m_gameRoot(std::move(gameRoot)) {}

bool SyncEngine::fileSymlinksAllowed() const
{
    if (m_fileSymlinksAllowed.has_value()) {
        return *m_fileSymlinksAllowed;
    }
    return SyncLink::canCreateFileSymlinks(m_profile.rootPath());
}

bool SyncEngine::isDirectory(const SyncItem& item) const
{
    const QFileInfo shared(m_profile.contentPath(item.path));
    if (shared.exists()) {
        return shared.isDir();
    }
    return item.kind == SyncItem::Kind::Directory;
}

SyncLink::Method SyncEngine::methodFor(const SyncItem& item) const
{
    const bool dir = isDirectory(item);
    // Only probe for file symlinks when the answer matters.
    const bool allowed = (dir || item.copyOnly) ? false : fileSymlinksAllowed();
    return SyncLink::chooseMethod(dir, item.copyOnly, allowed);
}

bool SyncEngine::allSucceeded(const QList<ItemResult>& results)
{
    return std::none_of(results.begin(), results.end(), [](const ItemResult& r) { return r.status == ItemResult::Status::Failed; });
}

QList<SyncEngine::ItemResult> SyncEngine::attach()
{
    QList<ItemResult> results;
    bool linkedWorlds = false;
    for (const auto& item : m_profile.items()) {
        auto result = attachItem(item);
        if (result.status == ItemResult::Status::Linked && (item.path == "saves" || item.path.startsWith("saves/"))) {
            linkedWorlds = true;
        }
        results.append(result);
    }
    if (linkedWorlds) {
        allowSymlinkedWorlds();
    }
    return results;
}

SyncEngine::ItemResult SyncEngine::attachItem(const SyncItem& item)
{
    ItemResult result;
    result.path = item.path;
    result.method = methodFor(item);

    const auto local = QDir(m_gameRoot).filePath(item.path);
    const auto shared = m_profile.contentPath(item.path);
    const QFileInfo localInfo(local);
    const bool localIsLink = SyncLink::isLink(local);
    const bool localExists = localInfo.exists() && !localIsLink;
    const bool sharedExists = QFileInfo::exists(shared);

    if (result.method == SyncLink::Method::Copy) {
        // A link left over from an earlier link-mode setup would make the copy write into the profile.
        if (localIsLink && !SyncLink::removeLink(local)) {
            result.error = QObject::tr("Could not remove the old link at %1").arg(local);
            return result;
        }
        QString error;
        if (sharedExists) {
            if (!SyncFiles::copyNewer(shared, local, &error)) {
                result.error = error;
                return result;
            }
        } else if (localExists) {
            // First instance to share this item seeds the profile.
            if (!SyncFiles::copyNewer(local, shared, &error)) {
                result.error = error;
                return result;
            }
        } else {
            result.status = ItemResult::Status::NothingToSync;
            return result;
        }
        result.status = ItemResult::Status::Copied;
        return result;
    }

    if (sharedExists && SyncLink::isLinkTo(local, shared)) {
        result.status = ItemResult::Status::Linked;
        return result;
    }

    QString error;
    if (!sharedExists) {
        if (localExists) {
            // First instance to share this item: move its content into the profile.
            if (!SyncFiles::movePath(local, shared, &error)) {
                result.error = error;
                return result;
            }
        } else if (isDirectory(item)) {
            QDir().mkpath(shared);
        } else {
            result.status = ItemResult::Status::NothingToSync;
            return result;
        }
    } else if (localExists) {
        // Joining a profile that already has this item: keep the instance's own copy aside.
        result.backupPath = SyncFiles::backupPathFor(local);
        if (!SyncFiles::movePath(local, result.backupPath, &error)) {
            result.error = error;
            result.backupPath.clear();
            return result;
        }
    }

    if (SyncLink::isLink(local) && !SyncLink::removeLink(local)) {
        result.error = QObject::tr("Could not remove the old link at %1").arg(local);
        return result;
    }
    if (!SyncLink::createLink(result.method, shared, local, &error)) {
        result.error = error;
        return result;
    }
    result.status = ItemResult::Status::Linked;
    return result;
}

QList<SyncEngine::ItemResult> SyncEngine::finish()
{
    QList<ItemResult> results;
    for (const auto& item : m_profile.items()) {
        if (methodFor(item) != SyncLink::Method::Copy) {
            continue;
        }
        ItemResult result;
        result.path = item.path;
        result.method = SyncLink::Method::Copy;
        const auto local = QDir(m_gameRoot).filePath(item.path);
        if (!QFileInfo::exists(local)) {
            result.status = ItemResult::Status::NothingToSync;
        } else if (SyncFiles::copyNewer(local, m_profile.contentPath(item.path), &result.error)) {
            result.status = ItemResult::Status::Copied;
        }
        results.append(result);
    }
    return results;
}

bool SyncEngine::detach(const SyncItem& item, QString* error)
{
    const auto local = QDir(m_gameRoot).filePath(item.path);
    const auto shared = m_profile.contentPath(item.path);
    if (!SyncLink::isLink(local)) {
        return true;  // copy-mode items already are standalone copies
    }
    if (!SyncLink::removeLink(local)) {
        setError(error, QObject::tr("Could not remove the link at %1").arg(local));
        return false;
    }
    if (!QFileInfo::exists(shared)) {
        return true;
    }
    return SyncFiles::copyNewer(shared, local, error);
}

void SyncEngine::allowSymlinkedWorlds()
{
    // Minecraft 1.20+ refuses to open worlds reached through a symlink unless the target is allowed here.
    const auto allowedFile = QDir(m_gameRoot).filePath("allowed_symlinks.txt");
    const auto line = QString("[prefix]%1/").arg(QDir::cleanPath(QFileInfo(m_profile.contentPath()).absoluteFilePath()));

    QByteArray content;
    QFile existing(allowedFile);
    if (existing.open(QIODevice::ReadOnly)) {
        content = existing.readAll();
        existing.close();
    }
    const auto lines = QString::fromUtf8(content).split('\n');
    if (lines.contains(line)) {
        return;
    }
    if (!content.isEmpty() && !content.endsWith('\n')) {
        content.append('\n');
    }
    content.append(line.toUtf8());
    content.append('\n');

    // Never write through a linked allowed_symlinks.txt into another instance.
    if (QFileInfo(allowedFile).isSymLink()) {
        QFile::remove(allowedFile);
    }
    QSaveFile out(allowedFile);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(content);
        out.commit();
    }
}
