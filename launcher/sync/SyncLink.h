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

#pragma once

#include <QString>

/**
 * @brief Low level helpers for linking shared sync content into an instance.
 *
 * None of the link types chosen here need admin rights:
 * - folders use directory symlinks, or directory junctions on Windows
 * - files use symlinks only where the OS allows it without elevation, and are copied otherwise
 */
namespace SyncLink {

enum class Method {
    DirectorySymlink,  ///< Linux, macOS
    Junction,          ///< Windows folders, no admin needed
    FileSymlink,       ///< Linux, macOS, Windows with Developer Mode
    Copy,              ///< copied in before launch, copied back after exit
};

QString methodName(Method method);

/// Picks the link type for an item. `fileSymlinksAllowed` is the result of canCreateFileSymlinks().
Method chooseMethod(bool isDirectory, bool copyOnly, bool fileSymlinksAllowed);

/// True when the OS lets this process create file symlinks without elevation. Always true outside Windows.
/// On Windows this probes once in `probeDir` (Developer Mode or an elevated process make it succeed).
bool canCreateFileSymlinks(const QString& probeDir);

/// Creates `linkPath` pointing at the existing `target` with the given method (not Copy).
bool createLink(Method method, const QString& target, const QString& linkPath, QString* error = nullptr);

/// True when `path` is a symlink or junction (dangling or not).
bool isLink(const QString& path);

/// True when `path` is a symlink or junction that resolves to `target`.
bool isLinkTo(const QString& path, const QString& target);

/// Removes the link itself, never what it points to.
bool removeLink(const QString& path);

}  // namespace SyncLink
