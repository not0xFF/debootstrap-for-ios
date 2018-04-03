/*
 * download.h
 *
 * Copyright (C) 2003 Bastian Blank <waldi@debian.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <debian-installer.h>

#include <stdio.h>

#include "package.h"
#include "suite.h"
#include "target.h"

static inline int build_target_deb_root (char *buf, size_t size, const char *file)
{
  snprintf (buf, size, "/var/cache/bootstrap/%s", file);
  return 0;
}

static inline int build_target_deb (char *buf, size_t size, const char *file)
{
  snprintf (buf, size, "%s/var/cache/bootstrap/%s", target_root, file);
  return 0;
}

static inline int build_indices (const char *file, char *source, size_t source_size, char *target, size_t target_size)
{
  snprintf (source, source_size, "dists/%s/main/binary-%s/%s", suite_name, arch, file);
  snprintf (target, target_size, "%s/var/cache/bootstrap/_dists_._main_binary-%s_%s", target_root, arch, file);
  return 0;
}

static inline void build_indices_root (const char *file, char *source, size_t source_size, char *target, size_t target_size)
{
  snprintf (source, source_size, "dists/%s/%s", suite_name, file);
  snprintf (target, target_size, "%s/var/cache/bootstrap/_dists_._%s", target_root, file);
}

int download (di_packages **packages, di_packages_allocator **allocator, di_slist **install);

int download_init (void);

#endif
