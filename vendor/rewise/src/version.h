/* This file is part of REWise.
 *
 * REWise is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * REWise is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef H_REWISE_VERSION
#define H_REWISE_VERSION

#define REWISE_VERSION_MAJOR 0
#define REWISE_VERSION_MINOR 3
#define REWISE_VERSION_PATCH 1

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

#ifdef REWISE_DEBUG
# define REWISE_VERSION_EXTRA "-debug"
#else
# define REWISE_VERSION_EXTRA
#endif

#define REWISE_VERSION_STR STRINGIFY(REWISE_VERSION_MAJOR) "." \
                           STRINGIFY(REWISE_VERSION_MINOR) "." \
                           STRINGIFY(REWISE_VERSION_PATCH) \
                           REWISE_VERSION_EXTRA

#endif
