/*
 * download.c
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

#include <config.h>

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "decompress.h"
#include "download.h"
#include "execute.h"
#include "frontend.h"
#include "gpg.h"
#include "package.h"
#include "target.h"

int decompress_file_gz (const char *file)
{
  int fd_in, fd_out;
  int ret = 1;
  char *file_in = alloca (strlen (file) + 4);

  sprintf (file_in, "%s.gz", file);

  fd_in = open (file_in, O_RDONLY);
  fd_out = open (file, O_WRONLY | O_CREAT, 0644);

  if (fd_in >= 0 && fd_out >= 0)
  {
    // There is no "read until EOF" flag, so use a large number
    struct decompress_gz *c = decompress_gz_new (fd_in, INT_MAX);
    while (decompress_gz (c, fd_out) > 0);
    decompress_gz_free (c);
    ret = 0;
  }

  close (fd_in);
  close (fd_out);

  return ret;
}

static int download_file (const char *source, const char *target, const char *message)
{
  log_message (LOG_MESSAGE_INFO_DOWNLOAD_RETRIEVE, message);
  return frontend_download (source, target);
}

static di_release *download_release (void)
{
  char source[256];
  char target[4096], sig_target[4096];
  const char *message = "InRelease";
  di_release *ret;

#if 0
  build_indices_root ("InRelease", source, sizeof (source), target, sizeof (target));

  if (!download_file (source, target, "InRelease"))
  {
    if (gpg_check_release (target, NULL, "InRelease"))
      log_message (authentication ? LOG_MESSAGE_ERROR_DOWNLOAD_VALIDATE : LOG_MESSAGE_WARNING_DOWNLOAD_VALIDATE, "InRelease");
  }
  else
  {
    log_message (LOG_MESSAGE_WARNING_DOWNLOAD_RETRIEVE, "InRelease");
#endif

    message = "Release";

    build_indices_root ("Release", source, sizeof (source), target, sizeof (target));
    if (download_file (source, target, "Release"))
      log_message (LOG_MESSAGE_ERROR_DOWNLOAD_RETRIEVE, "Release");

    build_indices_root ("Release.gpg", source, sizeof (source), sig_target, sizeof (sig_target));

    if (download_file (source, sig_target, "Release.gpg"))
    {
      if (authentication)
        log_message (LOG_MESSAGE_ERROR_DOWNLOAD_RETRIEVE, "Release.gpg");
    }
    else if (gpg_check_release (target, sig_target, "Release"))
      log_message (authentication ? LOG_MESSAGE_ERROR_DOWNLOAD_VALIDATE : LOG_MESSAGE_WARNING_DOWNLOAD_VALIDATE, "Release");
#if 0
  }
#endif

  log_message (LOG_MESSAGE_INFO_DOWNLOAD_PARSE, message);

  if (!(ret = di_release_read_file (target)))
    log_message (LOG_MESSAGE_ERROR_DOWNLOAD_PARSE, message);

  if (!suite && suite_use (ret->codename))
    return NULL;

  return ret;
}

static di_packages *download_packages_parse (const char *target, di_packages_allocator *allocator)
{
  log_message (LOG_MESSAGE_INFO_DOWNLOAD_PARSE, "Packages");

  return di_packages_minimal_read_file (target, allocator);
}

static di_packages *download_packages (di_release *rel, di_packages_allocator *allocator)
{
  char source[256];
  char target[4096];
  bool ok = false;
  struct stat statbuf;

  build_indices ("Packages.gz", source, sizeof (source), target, sizeof (target));
    
  /* yeah, the file already exists */
  if (!stat (target, &statbuf))
  {
    /* if it is invalid, unlink them */
    if (check_packages (target, ".gz", rel))
      unlink (target);
    else
      ok = true;
  }

  if (!ok)
  {
    /* try to download the gzip compressed version ... */
    if (download_file (source, target, "Packages.gz"))
      log_message (LOG_MESSAGE_ERROR_DOWNLOAD_RETRIEVE, "Packages");
    if (check_packages (target, ".gz", rel))
      log_message (LOG_MESSAGE_ERROR_DOWNLOAD_VALIDATE, "Packages");
  }

  build_indices ("Packages", 0, 0, target, sizeof (target));

  /* ... decompress it ... */
  if (decompress_file_gz (target))
    log_message (LOG_MESSAGE_ERROR_DECOMPRESS, "Packages.gz");

  /* ... and parse them */
  return download_packages_parse (target, allocator);
}

static di_packages *download_indices (di_packages_allocator *allocator)
{
  di_release *rel;
  di_packages *ret;

  rel = download_release ();
  if (!rel)
    return NULL;

  frontend_progress_set (10);

  ret = download_packages (rel, allocator);

  frontend_progress_set (50);
  return ret;
}

static int download_debs (di_slist *install)
{
  int count = 0, ret, size = 0, size_done = 0, progress;
  struct stat statbuf;
  di_slist_node *node;
  di_package *p;
  char target[4096];

  for (node = install->head; node; node = node->next)
  {
    p = node->data;
    count++;
    size += p->size;
  }

  for (node = install->head; node; node = node->next)
  {
    p = node->data;
    size_done += p->size;
    progress = ((float) size_done/size) * 350 + 50;

    build_target_deb (target, sizeof (target), package_get_local_filename (p));

    if (!stat (target, &statbuf))
    {
      ret = check_deb (target, p, p->package);
      if (!ret)
      {
        frontend_progress_set (progress);
        continue;
      }
    }

    if (download_file (p->filename, target, p->package) || check_deb (target, p, p->package))
      log_message (LOG_MESSAGE_ERROR_DOWNLOAD_RETRIEVE, p->filename);

    frontend_progress_set (progress);
  }

  return 0;
}

int download (di_packages **packages, di_packages_allocator **allocator, di_slist **install)
{
  *allocator = di_packages_allocator_alloc ();
  if (!*allocator)
    return 1;

  *packages = download_indices (*allocator);
  if (!*packages)
    return 1;

  (*install) = suite_packages_list (*packages, *allocator);
  if (!*install)
    log_text (DI_LOG_LEVEL_ERROR, "Couldn't build installation list");

  return download_debs (*install);
}

int download_init (void)
{
  target_create_dir ("/var");
  target_create_dir ("/var/cache");
  target_create_dir ("/var/cache/bootstrap");
  return 0;
}

