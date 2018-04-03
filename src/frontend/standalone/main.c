/*
 * main.c
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

#define _GNU_SOURCE

#include <config.h>

#include "download.h"
#include "execute.h"
#include "frontend.h"
#include "gpg.h"
#include "install.h"
#include "message.h"
#include "log.h"
#include "suite.h"
#include "target.h"

#include <debian-installer.h>

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *target_root;
FILE *foreign_script;

static enum
{
    MIRROR_SCHEME_HTTP,
    MIRROR_SCHEME_FTP,
    MIRROR_SCHEME_FILE,
    MIRROR_SCHEME_SSH,
} mirror_scheme;
static char mirror_host[128];
static char mirror_path[256];

#ifdef DEB_ARCH
const char default_arch[] = DEB_ARCH;
#endif
const char default_configdir[] = CONFIGDIR;
const char default_flavour[] = "standard";
const char default_mirror[] = "http://ftp.debian.org/debian";

enum
{
  GETOPT_FIRST = CHAR_MAX + 1,
  GETOPT_ALLOW_UNAUTHENTICATED,
  GETOPT_DEBUG,
  GETOPT_EXCLUDE,
  GETOPT_FOREIGN,
  GETOPT_INCLUDE,
  GETOPT_SUITE_CONFIG,
  GETOPT_VARIANT,
  GETOPT_VERSION,
};

static struct option const long_opts[] =
{
  {"allow-unauthenticated", no_argument, 0, GETOPT_ALLOW_UNAUTHENTICATED},
  {"arch", required_argument, 0, 'a'},
  {"configdir", required_argument, 0, 'c'},
  {"debug", no_argument, 0, GETOPT_DEBUG},
  {"download-only", no_argument, 0, 'd'},
  {"exclude", required_argument, 0, GETOPT_EXCLUDE},
  {"foreign", no_argument, 0, GETOPT_FOREIGN},
  {"flavour", required_argument, 0, 'f'},
  {"helperdir", required_argument, 0, 'H'},
  {"include", required_argument, 0, GETOPT_INCLUDE},
  {"keyring", required_argument, 0, 'k'},
  {"quiet", no_argument, 0, 'q'},
  {"suite-config", required_argument, 0, GETOPT_SUITE_CONFIG},
  {"variant", required_argument, 0, GETOPT_VARIANT},
  {"verbose", no_argument, 0, 'v'},
  {"help", no_argument, 0, 'h'},
  {"version", no_argument, 0, GETOPT_VERSION},
  {0, 0, 0, 0}
};

char *program_name;

int frontend_download (const char *source, const char *target)
{
  char buf[1024];
  int ret;

  switch (mirror_scheme)
  {
    case MIRROR_SCHEME_HTTP:
    case MIRROR_SCHEME_FTP:
      {
        const char *scheme = "http";

        if (mirror_scheme == MIRROR_SCHEME_FTP)
          scheme = "ftp";

        snprintf (buf, sizeof buf, "wget -q -O %s %s://%s%s/%s", target, scheme, mirror_host, mirror_path, source);
      }
      break;

    case MIRROR_SCHEME_FILE:
      snprintf (buf, sizeof buf, "cp %s/%s %s", mirror_path, source, target);
      break;

    case MIRROR_SCHEME_SSH:
      snprintf (buf, sizeof buf, "ssh -o BatchMode=yes %s 'cat %s/%s' > %s", mirror_host, mirror_path, source, target);
  }

  ret = execute_sh (buf);

  return WEXITSTATUS (ret);
}

static void check_mirror_scheme (const char **input)
{
  const char *temp = *input;

  while (*temp >= 'a' && *temp <= 'z')
    temp++;

  if (*temp++ != ':' || *temp++ != '/' || *temp++ != '/')
    log_text (DI_LOG_LEVEL_ERROR, "Invalid mirror: can't find scheme");

  if (!strncmp (*input, "http", 4))
    mirror_scheme = MIRROR_SCHEME_HTTP;
  else if (!strncmp (*input, "ftp", 3))
    mirror_scheme = MIRROR_SCHEME_FTP;
  else if (!strncmp (*input, "file", 4))
    mirror_scheme = MIRROR_SCHEME_FILE;
  else if (!strncmp (*input, "ssh", 3))
    mirror_scheme = MIRROR_SCHEME_SSH;
  else
    log_text (DI_LOG_LEVEL_ERROR, "Invalid mirror: scheme not supported");

  *input = temp;
}

static void check_mirror_host (const char **input)
{
  const char *temp = *input;

  switch (mirror_scheme)
  {
    case MIRROR_SCHEME_FILE:
      if (*temp != '/')
        log_text (DI_LOG_LEVEL_ERROR, "Invalid mirror: file scheme must not include a host");
      break;

    default:
      if (*temp == '/')
        log_text (DI_LOG_LEVEL_ERROR, "Invalid mirror: scheme must include a host");
      while (*temp && *temp != '/')
        temp++;
      strncpy (mirror_host, *input, temp - *input);
  }

  *input = temp;
}

static void check_mirror_path (const char *input)
{
  size_t len = strlen (input);

  while (len > 0 && input[len - 1] == '/')
    len--;

  strncpy (mirror_path, input, len);
}

static void check_mirror (const char *mirror)
{
  const char *temp = mirror;

  check_mirror_scheme (&temp);
  check_mirror_host (&temp);
  check_mirror_path (temp);
}

static void check_permission (bool noexec)
{
  if (!noexec && getuid ())
    log_text (DI_LOG_LEVEL_ERROR, "Need root privileges");
}

static void check_target (const char *target, bool noexec)
{
  struct stat s;
  struct statvfs sv;

  if (stat (target, &s) == 0)
  {
    if (!(S_ISDIR (s.st_mode)))
      log_text (DI_LOG_LEVEL_ERROR, "Target exists but is no directory");
  }
  else if (errno == ENOENT)
  {
    if (mkdir (target, 0777))
      log_text (DI_LOG_LEVEL_ERROR, "Failed to create target");
  }
  else
    log_text (DI_LOG_LEVEL_ERROR, "Target check failed: %s", strerror (errno));

  if (statvfs (target, &sv) == 0)
  {
    if (sv.f_flag & ST_RDONLY)
      log_text (DI_LOG_LEVEL_ERROR, "Target is readonly");
    if (sv.f_flag & ST_NODEV && !noexec)
      log_text (DI_LOG_LEVEL_ERROR, "Target disallows device special files");
    if (sv.f_flag & ST_NOEXEC && !noexec)
      log_text (DI_LOG_LEVEL_ERROR, "Target disallows program execution");
  }
  else
    log_text (DI_LOG_LEVEL_ERROR, "Target check failed: %s", strerror (errno));

  target_root = canonicalize_file_name(target);
  if (!target_root)
    log_text (DI_LOG_LEVEL_ERROR, "Target check failed: %s", strerror (errno));
}

static int finish_etc_apt_sources_list (void)
{
  char file[PATH_MAX];
  FILE *out;
  char line[1024];
  const char *scheme = NULL;

  switch (mirror_scheme)
  {
    case MIRROR_SCHEME_HTTP:
      scheme = "http";
      break;
    case MIRROR_SCHEME_FTP:
      scheme = "ftp";
      break;
    case MIRROR_SCHEME_FILE:
      return 0;
    case MIRROR_SCHEME_SSH:
      scheme = "ssh";
      break;
  }

  snprintf (line, sizeof line, "deb %s://%s%s %s main", scheme, mirror_host, mirror_path, suite_name);

  if (foreign_script)
    fprintf (foreign_script, "\necho '%s' > /etc/apt/sources.list\n\n", line);
  else
  {
    log_text (DI_LOG_LEVEL_MESSAGE, "Writing apt sources.list");

    strcpy (file, target_root);
    strcat (file, "/etc/apt/sources.list");

    out = fopen (file, "w");
    if (!out)
      return 1;

    if (!fprintf (out, "%s\n", line))
      return 1;

    if (fclose (out))
      return 1;
  }

  return 0;
}

static int finish_etc_hosts (void)
{
  char file[PATH_MAX];
  FILE *out;

  log_text (DI_LOG_LEVEL_MESSAGE, "Writing hosts");

  strcpy (file, target_root);
  strcat (file, "/etc/hosts");

  out = fopen (file, "w");
  if (!out)
    return 1;

  if (!fputs ("127.0.0.1 localhost\n", out))
    return 1;

  if (fclose (out))
    return 1;

  return 0;
}

static int finish_etc_resolv_conf (void)
{
  char file_in[PATH_MAX], file_out[PATH_MAX], buf[1024];
  FILE *in, *out;
  struct stat s;

  strcpy (file_in, "/etc/resolv.conf");
  strcpy (file_out, target_root);
  strcat (file_out, "/etc/resolv.conf");

  if (!stat (file_in, &s))
  {
    log_text (DI_LOG_LEVEL_MESSAGE, "Writing resolv.conf");

    in = fopen (file_in, "r");
    out = fopen (file_out, "w");
    if (!in || !out)
      return 1;

    while (1)
    {
      size_t len = fread (buf, 1, sizeof buf, in);
      if (!len)
        break;
      fwrite (buf, 1, len, out);
    }

    if (fclose (in) || fclose (out))
      return 1;
  }

  return 0;
}

static int finish_etc (void)
{
  return finish_etc_apt_sources_list () ||
         finish_etc_hosts () ||
         finish_etc_resolv_conf ();
}

static const char *generate_configdir ()
{
#ifdef CONFIGDIR_BINARY
  static char binary_configdir[4096];
  char dir_temp[strlen (program_name) + 1], *dir;

  strcpy (dir_temp, program_name);
  dir = dirname (dir_temp);
  strcpy (binary_configdir, dir);
  strcat (binary_configdir, "/");
  strcat (binary_configdir, default_configdir);
  return binary_configdir;
#else
  return default_configdir;
#endif
}

static int foreign_finish ()
{
  char buf1[PATH_MAX], buf2[PATH_MAX];
  int ret;
  
  snprintf (buf1, sizeof buf1, "%s/sbin/init", target_root);
  snprintf (buf2, sizeof buf2, "%s/sbin/init.foreign", target_root);

  ret = rename (buf1, buf2);
  if (ret)
    return ret;

  ret = symlink ("cdebootstrap-foreign", buf1);
  if (ret)
    return ret;

  fputs ("\
run rm /sbin/init\n\
run dpkg-divert --remove --local --rename --divert /sbin/init.REAL /sbin/init\n\
run rm /sbin/cdebootstrap-foreign\n\
\n\
finish\n\
", foreign_script);

  log_text (DI_LOG_LEVEL_INFO, "Second stage installer is available as /sbin/cdebootstrap-foreign or /sbin/init");

  return 0;
}

static install_simulate_handler foreign_handler;
static int foreign_handler (const char *const command[])
{
  fputs ("run ", foreign_script);
  for (; *command; command++)
  {
    fputs (*command, foreign_script);
    fputs (" ", foreign_script);
  }
  fputs ("\n", foreign_script);
  return 0;
}

static int foreign_init ()
{
  char buf[PATH_MAX];
  int ret;
  
  target_create_dir ("/sbin");

  snprintf (buf, sizeof buf, "%s/sbin/cdebootstrap-foreign", target_root);
  foreign_script = fopen (buf, "w");

  if (!foreign_script)
    return 1;

  ret = chmod (buf, 0755);
  if (ret)
    return ret;

  fputs ("\
#!/bin/sh\n\
\n\
", foreign_script);

  const char *const *temp = execute_environment_target;
  while (*temp)
    fprintf (foreign_script, "export %s\n", *temp++);

  fputs ("\
\n\
exec_pid1() {\n\
  [ \"$$\" = 1 ] && exec env -u DEBIAN_FRONTEND \"$@\" || :\n\
}\n\
\n\
finish() {\n\
  exec_pid1 /sbin/init\n\
  umount /proc || :\n\
  exit 0\n\
}\n\
\n\
error() {\n\
  echo \"$1\" 2>&1\n\
  exec_pid1 /bin/sh\n\
  umount /proc || :\n\
  exit 1\n\
}\n\
\n\
run() {\n\
  \"$@\" 3>/dev/null || error \"Failed to run $1!\"\n\
}\n\
\n\
trap 'error \"Interruped!\"' HUP INT TERM\n\
\n\
mount -n -o remount,rw rootfs /\n\
\n\
chown -hR 0:0 /\n\
\n\
mount -n -t proc none /proc\n\
\n\
if [ ! -e /sbin/init.REAL ]; then\n\
  run rm /sbin/init\n\
  run ln -s /bin/sh /sbin/init\n\
  run dpkg-divert --add --local --divert /sbin/init.REAL /sbin/init\n\
  run mv /sbin/init.foreign /sbin/init.REAL\n\
fi\n\
\n\
", foreign_script);

  return 0;
}

static void usage (int status)
{
  if (status != 0)
    fprintf (stderr, "Try `%s --help' for more information.\n", program_name);
  else
  {
    fprintf (stdout, "\
Usage: %s [OPTION]... SUITE TARGET [MIRROR]\n\
\n\
", program_name);
    fputs ("\
Mandatory arguments to long options are mandatory for short options too.\n\
", stdout);
    fputs ("\
      --allow-unauthenticated  Ignore if packages can’t be authenticated.\n\
  -a, --arch=ARCH              Set the target architecture.\n\
  -c, --configdir=DIR          Set the config directory.\n\
      --debug                  Enable debug output.\n\
  -d, --download-only          Download packages, but don't perform installation.\n\
      --exclude=A,B,C          Drop packages from the installation list\n\
  -f, --flavour=FLAVOUR        Select the flavour to use.\n\
      --foreign                Use two stage installer.\n\
  -k, --keyring=KEYRING        Use given keyring.\n\
  -H, --helperdir=DIR          Set the helper directory.\n\
      --include=A,B,C          Install extra packages.\n\
  -q, --quiet                  Be quiet.\n\
      --suite-config\n\
  -v, --verbose                Be verbose,\n\
  -h, --help                   Display this help and exit.\n\
      --version                Output version information and exit.\n\
", stdout);
    fputs ("\n\
Defines:\n\
target architecture: " 
#ifdef DEB_ARCH
DEB_ARCH 
#else
"(no default)"
#endif
"\n\
config and helper directory: "
#if CONFIGDIR_BINARY
"path of binary/"
#endif
CONFIGDIR
"\n", stdout);
  }
  exit (status);
}

int frontend_main (int argc, char **argv, char **envp __attribute((unused)))
{
  int c;
  const char
    *arch = default_arch,
    *configdir,
    *flavour = default_flavour,
    *keyring = NULL,
    *helperdir,
    *mirror = NULL,
    *suite = NULL,
    *suite_config = NULL,
    *target = NULL;
  bool authentication = true, download_only = false, foreign = false;
  di_slist include = { NULL, NULL }, exclude = { NULL, NULL };
  const char *keyringdirs[] =
  {
    "/usr/share/keyrings",
    NULL, NULL
  };
  static di_packages *packages;
  static di_packages_allocator *allocator;
  static di_slist *list;

  program_name = argv[0];

  configdir = helperdir = keyringdirs[1] = generate_configdir ();

  while ((c = getopt_long (argc, argv, "a:c:df:hH:i:k:s:qv", long_opts, NULL)) != -1)
  {
    switch (c)
    {
      case 0:
        break;
      case 'a':
        arch = optarg;
        break;
      case 'c':
        configdir = optarg;
        break;
      case 'd':
        download_only = true;
        break;
      case 'f':
        flavour = optarg;
        break;
      case 'h':
        usage (EXIT_SUCCESS);
        break;
      case 'H':
        helperdir = optarg;
        break;
      case 'k':
        keyring = optarg;
        break;
      case 'q':
        if (message_level <= MESSAGE_LEVEL_NORMAL)
          message_level = MESSAGE_LEVEL_QUIET;
        break;
      case 'v':
        if (message_level <= MESSAGE_LEVEL_NORMAL)
          message_level = MESSAGE_LEVEL_VERBOSE;
        break;
      case GETOPT_ALLOW_UNAUTHENTICATED:
        authentication = false;
        break;
      case GETOPT_DEBUG:
        message_level = MESSAGE_LEVEL_DEBUG;
        break;
      case GETOPT_EXCLUDE:
        {
          char *l = strdup (optarg);
          for (char *i = strtok (l, ","); i; i = strtok (NULL, ","))
            di_slist_append (&exclude, i);
        }
        break;
      case GETOPT_FOREIGN:
        foreign = true;
        break;
      case GETOPT_INCLUDE:
        {
          char *l = strdup (optarg);
          for (char *i = strtok (l, ","); i; i = strtok (NULL, ","))
            di_slist_append (&include, i);
        }
        break;
      case GETOPT_SUITE_CONFIG:
        suite_config = optarg;
        break;
      case GETOPT_VARIANT:
        if (!strcmp(optarg, "buildd"))
          flavour = "build";
        else if (!strcmp(optarg, "fakechroot"))
          flavour = "standard";
        else
          log_text(DI_LOG_LEVEL_ERROR, "Invalid flavour");
        break;
      case GETOPT_VERSION:
        fputs (PACKAGE_STRING, stdout);
        fputs ("\n\n\
This is free software; see the source for copying conditions.  There is NO\n\
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\
", stdout);
        exit (EXIT_SUCCESS);
        break;
      default:
        usage (EXIT_FAILURE);
    }
  }

  if ((argc - optind) <= 0)
  {
    fprintf (stderr, "%s: missing suite argument\n", program_name);
    usage (EXIT_FAILURE);
  }
  else if ((argc - optind) == 1)
  {
    fprintf (stderr, "%s: missing target argument\n", program_name);
    usage (EXIT_FAILURE);
  }
  else if ((argc - optind) > 3)
  {
    fprintf (stderr, "%s: too much arguments\n", program_name);
    usage (EXIT_FAILURE);
  }
  else if ((argc - optind) == 3)
  {
    mirror = argv[optind + 2];
  }
  else
  {
    mirror = default_mirror;
  }

  suite = argv[optind];
  target = argv[optind + 1];

#ifndef DEB_ARCH
  if (!arch)
  {
    fprintf (stderr, "%s: missing architecture\n", program_name);
    usage (EXIT_FAILURE);
  }
#endif

  di_init (basename (program_name));

  umask (022);

  check_mirror (mirror);
  check_permission (download_only || foreign);
  check_target (target, download_only || foreign);

  log_init ();

  if (suite_init (suite, suite_config, arch, flavour, &include, &exclude, configdir, authentication))
    log_text (DI_LOG_LEVEL_ERROR, "Internal error: suite init");
  if (gpg_init (keyringdirs, keyring))
    log_text (DI_LOG_LEVEL_ERROR, "Internal error: gpg init");
  if (download_init ())
    log_text (DI_LOG_LEVEL_ERROR, "Internal error: download init");
  if (foreign && foreign_init ())
    log_text (DI_LOG_LEVEL_ERROR, "Internal error: foreign init");

  if (download (&packages, &allocator, &list))
    log_text (DI_LOG_LEVEL_ERROR, "Internal error: download");

  if (download_only)
  {
    log_text (DI_LOG_LEVEL_INFO, "Download-only mode, not installing anything");
    return 0;
  }

  install_simulate_handler *tmp = NULL;
  if (foreign)
    tmp = foreign_handler;
  if (install_init (helperdir, tmp))
    log_text (DI_LOG_LEVEL_ERROR, "Internal error: install init");

  if (install (packages, allocator, list))
    log_text (DI_LOG_LEVEL_ERROR, "Internal error: install");

  finish_etc ();

  if (foreign && foreign_finish ())
    log_text (DI_LOG_LEVEL_ERROR, "Internal error: foreign finish");

  return 0;
}

