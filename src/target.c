/*
 * target.c
 *
 * Copyright (C) 2003-2008 Bastian Blank <waldi@debian.org>
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

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"
#include "target.h"

void target_create_dir (const char *dir)
{
  char buf[PATH_MAX];

  snprintf (buf, sizeof buf, "%s/%s", target_root, dir);

  if (mkdir (buf, 0755) < 0 && errno != EEXIST)
    log_text (DI_LOG_LEVEL_ERROR, "Directory creation failed: %s", strerror (errno));
}

void target_create_file (const char *file)
{
  char buf[PATH_MAX];
  int fd;

  snprintf (buf, sizeof buf, "%s/%s", target_root, file);

  fd = open (buf, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  if (fd < 0)
    log_text (DI_LOG_LEVEL_ERROR, "File creation failed: %s", strerror (errno));

  close (fd);
}
