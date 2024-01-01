/*
 * Copyright (C) 2020 KeePassXC Team <team@keepassxc.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 or (at your option)
 * version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef KEEPASSXC_OSUTILS_H
#define KEEPASSXC_OSUTILS_H

#if defined(Q_OS_WIN)

#include "winutils/WinUtils.h"
#define osUtils static_cast<OSUtilsBase*>(winUtils())

#elif defined(Q_OS_MACOS)

#include "macutils/MacUtils.h"
#define osUtils static_cast<OSUtilsBase*>(macUtils())

#elif defined(Q_OS_UNIX)

#include "nixutils/NixUtils.h"
#define osUtils static_cast<OSUtilsBase*>(nixUtils())

#endif

#endif // KEEPASSXC_OSUTILS_H
