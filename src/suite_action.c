/*
 * suite.c
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

#include "execute.h"
#include "frontend.h"
#include "install.h"
#include "package.h"
#include "suite.h"
#include "suite_action.h"
#include "suite_config.h"

#include <debian-installer.h>

static di_slist *cur_list;
static di_package_priority cur_list_priority;

typedef int suite_install_action (suite_config_action *action, int data, di_packages *packages, di_packages_allocator *allocator, di_slist *install);

static di_package_priority action_check_priority (const char *text)
{
  if (text)
    return di_package_priority_text_from (text);
  return 0;
}

static void action_check_priority_list (di_package_priority priority, di_packages *packages, di_packages_allocator *allocator, di_slist *install)
{
  if (cur_list_priority != priority)
  {
    if (cur_list)
      di_slist_free (cur_list);
    cur_list = install_list (packages, allocator, install, priority, di_package_status_installed);
    cur_list_priority = priority;
  }
}

enum action_install
{
  ACTION_INSTALL_APT_INSTALL,
  ACTION_INSTALL_DPKG_CONFIGURE,
  ACTION_INSTALL_DPKG_INSTALL,
  ACTION_INSTALL_DPKG_UNPACK,
  ACTION_INSTALL_EXTRACT,
};

static int action_install (suite_config_action *action, int _data, di_packages *packages, di_packages_allocator *allocator, di_slist *install)
{
  enum action_install data = _data;

  int force = action->flags & SUITE_CONFIG_ACTION_FLAG_FORCE;
  int only = action->flags & SUITE_CONFIG_ACTION_FLAG_ONLY;
  di_package_priority priority;
  di_slist *list;
  int cleanup = 0;

  if (action->what)
    priority = action_check_priority (action->what);
  else if (data == ACTION_INSTALL_EXTRACT)
    priority = di_package_priority_required;
  else
    priority = di_package_priority_extra;
  action_check_priority_list (priority, packages, allocator, install);

  if (priority)
    list = cur_list;
  else if (action->what)
  {
    if (only)
      list = install_list_package_only (packages, action->what, di_package_status_installed);
    else
      list = install_list_package (packages, allocator, action->what, di_package_status_installed);
    cleanup = 1;
  }
  else if (data == ACTION_INSTALL_DPKG_CONFIGURE)
    list = NULL;
  else
    return 0;

  switch (data)
  {
    case ACTION_INSTALL_APT_INSTALL:
      return install_apt_install (packages, list);

    case ACTION_INSTALL_DPKG_CONFIGURE:
      return install_dpkg_configure (packages, force);

    case ACTION_INSTALL_DPKG_INSTALL:
      return install_dpkg_install (packages, list, force);

    case ACTION_INSTALL_DPKG_UNPACK:
      return install_dpkg_unpack (packages, list);

    case ACTION_INSTALL_EXTRACT:
      return install_extract (list);
  }
  
  if (cleanup)
    di_slist_free (list);

  return 0;
}

static int action_helper_install (suite_config_action *action, int data __attribute__ ((unused)), di_packages *packages __attribute__ ((unused)), di_packages_allocator *allocator __attribute__ ((unused)), di_slist *install __attribute__ ((unused)))
{
  return install_helper_install (action->what);
}

static int action_helper_remove (suite_config_action *action, int data __attribute__ ((unused)), di_packages *packages __attribute__ ((unused)), di_packages_allocator *allocator __attribute__ ((unused)), di_slist *install __attribute__ ((unused)))
{
  return install_helper_remove (action->what);
}

static int action_mount (suite_config_action *action, int data __attribute__ ((unused)), di_packages *packages __attribute__ ((unused)), di_packages_allocator *allocator __attribute__ ((unused)), di_slist *install __attribute__ ((unused)))
{
  return install_mount (action->what);
}

static struct suite_install_actions
{
  char *name;
  suite_install_action *action;
  int data;
}
suite_install_actions[] =
{
  { "apt-install", action_install, ACTION_INSTALL_APT_INSTALL },
  { "dpkg-configure", action_install, ACTION_INSTALL_DPKG_CONFIGURE },
  { "dpkg-install", action_install, ACTION_INSTALL_DPKG_INSTALL },
  { "dpkg-unpack", action_install, ACTION_INSTALL_DPKG_UNPACK },
  { "extract", action_install, ACTION_INSTALL_EXTRACT },
  { "helper-install", action_helper_install, 0 },
  { "helper-remove", action_helper_remove, 0 },
  { "mount", action_mount, 0 },
  { NULL, NULL, 0 },
};

int suite_action (di_packages *packages, di_packages_allocator *allocator, di_slist *install)
{
  di_slist_node *node;
  struct suite_install_actions *action;

  for (node = suite->actions.head; node; node = node->next)
  {
    suite_config_action *e = node->data;

    if (!e->activate)
      continue;

    for (action = suite_install_actions; action->name; action++)
      if (!strcasecmp (action->name, e->action))
        break;

    if (action->name)
    {
      log_text (DI_LOG_LEVEL_DEBUG, "call action: %s", e->action);
      if (action->action (e, action->data, packages, allocator, install))
        return 1;
    }
    else
      log_text (DI_LOG_LEVEL_WARNING, "Unknown action: %s", e->action);
  }

  return 0;
}

