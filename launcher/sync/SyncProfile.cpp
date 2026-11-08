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

#include "SyncProfile.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSaveFile>

namespace {
constexpr int FORMAT_VERSION = 1;

QString profileFile(const QString& rootPath)
{
    return QDir(rootPath).filePath("profile.json");
}
}  // namespace

SyncProfile::SyncProfile(QString id, QString rootPath) : m_id(std::move(id)), m_rootPath(std::move(rootPath)) {}

QString SyncProfile::contentPath() const
{
    return QDir(m_rootPath).filePath("content");
}

QString SyncProfile::contentPath(const QString& relativePath) const
{
    return QDir(contentPath()).filePath(relativePath);
}

QString SyncProfile::normalizeItemPath(const QString& path)
{
    QString p = QDir::fromNativeSeparators(path.trimmed());
    if (p.isEmpty() || QDir::isAbsolutePath(p) || p.contains(':')) {
        return {};
    }
    p = QDir::cleanPath(p);
    while (p.endsWith('/')) {
        p.chop(1);
    }
    if (p.isEmpty() || p == "." || p == ".." || p.startsWith("../")) {
        return {};
    }
    return p;
}

bool SyncProfile::addItem(const SyncItem& item)
{
    SyncItem clean = item;
    clean.path = normalizeItemPath(item.path);
    if (clean.path.isEmpty()) {
        return false;
    }
    for (auto& existing : m_items) {
        if (existing.path == clean.path) {
            existing = clean;
            return true;
        }
    }
    m_items.append(clean);
    return true;
}

bool SyncProfile::removeItem(const QString& path)
{
    const auto clean = normalizeItemPath(path);
    return m_items.removeIf([&clean](const SyncItem& item) { return item.path == clean; }) > 0;
}

bool SyncProfile::load(const QString& rootPath, SyncProfile& out, QString* error)
{
    QFile file(profileFile(rootPath));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QObject::tr("Could not open %1: %2").arg(file.fileName(), file.errorString());
        }
        return false;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = QObject::tr("Invalid sync profile %1: %2").arg(file.fileName(), parseError.errorString());
        }
        return false;
    }
    const auto root = doc.object();

    SyncProfile profile(root.value("id").toString(QDir(rootPath).dirName()), rootPath);
    profile.m_name = root.value("name").toString();
    for (const auto& value : root.value("items").toArray()) {
        const auto obj = value.toObject();
        SyncItem item;
        item.path = obj.value("path").toString();
        item.kind = obj.value("kind").toString() == "file" ? SyncItem::Kind::File : SyncItem::Kind::Directory;
        item.copyOnly = obj.value("copyOnly").toBool(false);
        profile.addItem(item);  // drops invalid paths
    }
    out = profile;
    return true;
}

bool SyncProfile::save(QString* error) const
{
    if (!QDir().mkpath(contentPath())) {
        if (error) {
            *error = QObject::tr("Could not create %1").arg(contentPath());
        }
        return false;
    }

    QJsonArray items;
    for (const auto& item : m_items) {
        QJsonObject obj;
        obj.insert("path", item.path);
        obj.insert("kind", item.kind == SyncItem::Kind::File ? "file" : "directory");
        if (item.copyOnly) {
            obj.insert("copyOnly", true);
        }
        items.append(obj);
    }
    QJsonObject root;
    root.insert("formatVersion", FORMAT_VERSION);
    root.insert("id", m_id);
    root.insert("name", m_name);
    root.insert("items", items);

    QSaveFile file(profileFile(m_rootPath));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0 || !file.commit()) {
        if (error) {
            *error = QObject::tr("Could not write %1: %2").arg(file.fileName(), file.errorString());
        }
        return false;
    }
    return true;
}
