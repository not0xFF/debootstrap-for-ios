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

#include "gpg.h"
#include "install.h"
#include "log.h"
#include "suite.h"
#include "suite_action.h"
#include "suite_config.h"

#include <debian-installer.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

struct suites_config_suite *suites_suite = NULL;
struct suite_config *suite = NULL;
const char *arch = NULL;
const char *flavour = NULL;
const char *suite_name = NULL;
static di_slist *include, *exclude;
static const char *configdir;
bool authentication = true;

static void suite_init_check_action (suite_config_action *action)
{
  di_slist_node *node;

  if (action->flavour.head)
    for (node = action->flavour.head; node; node = node->next)
    {
      if (!strcasecmp (flavour, node->data))
      {
        action->activate = true;
        break;
      }
    }
  else
    action->activate = true;
}

static void suite_init_check_section (void *key __attribute__ ((unused)), void *data, void *user_data)
{
  suite_config *config = user_data;
  suite_config_section *section = data;
  di_slist_node *node1, *node2;

  if (section->flavour.head)
    for (node1 = section->flavour.head; node1; node1 = node1->next)
    {
      if (!config->flavour_valid &&
          !strcasecmp (flavour, node1->data))
        config->flavour_valid = true;
      if (!strcasecmp (flavour, node1->data))
      {
        section->activate = true;
        break;
      }
    }
  else
    section->activate = true;

  if (section->activate)
    for (node1 = section->packages.head; node1; node1 = node1->next)
    {
      suite_config_packages *packages = node1->data;

      if (packages->arch.head)
      {
        for (node2 = packages->arch.head; node2; node2 = node2->next)
          if (!strcasecmp ("any", node2->data) ||
              !strcasecmp (arch, node2->data))
          {
            packages->activate = true;
            break;
          }
      }
      else
        packages->activate = true;
    }
}

static int suite_check (void);

int suite_init (const char *_suite_name, const char *suite_config_name, const char *_arch, const char *_flavour, di_slist *_include, di_slist *_exclude, const char *_configdir, bool _authentication)
{
  arch = _arch;
  flavour = _flavour;
  suite_name = _suite_name;
  include = _include;
  exclude = _exclude;
  configdir = _configdir;
  authentication = _authentication;

  log_text (DI_LOG_LEVEL_DEBUG, "Init suite %s", suite_name);
  if (suite_config_name)
    log_text (DI_LOG_LEVEL_DEBUG, "Init suite config %s", suite_config_name);

  if (suite_config_init (suite_config_name ? suite_config_name : suite_name, configdir))
    return 1;
  if (suite)
    return suite_check ();
  return 0;
}
  
int suite_use (const char *_suite_name)
{
  if (suite)
  {
    log_text (DI_LOG_LEVEL_DEBUG, "Ignore suite use for %s", suite_name);
    return 0;
  }

  suite_name = _suite_name;

  log_text (DI_LOG_LEVEL_DEBUG, "Use suite %s", suite_name);

  if (suite_config_init_second (suite_name, configdir))
    return 1;

  return suite_check ();
}

static int suite_check (void)
{
  di_slist_node *node;

  for (node = suite->actions.head; node; node = node->next)
    suite_init_check_action (node->data);
  di_hash_table_foreach (suite->sections, suite_init_check_section, suite);

  if (!suite->flavour_valid)
    log_text (DI_LOG_LEVEL_ERROR, "Unknown flavour %s", flavour);

  return 0;
}

struct suite_packages_list_user_data
{
  di_tree *include, *exclude;
  di_slist list;
  di_packages *packages;
  bool select_priority_required;
  bool select_priority_important;
};

static int suite_packages_list_cmp (const void *key1, const void *key2)
{
  const char *k1 = key1;
  const char *k2 = key2;
  return strcmp (k1, k2);
}

static void suite_packages_list_add_exclude (struct suite_packages_list_user_data *user_data, const char *name)
{
  di_package *p = di_packages_get_package (user_data->packages, name, 0);
  if (p)
    di_tree_insert (user_data->exclude, p->package, p);
  else
    log_text (DI_LOG_LEVEL_MESSAGE, "Can't find package %s for exclusion", name);
}

static void suite_packages_list_add_include (struct suite_packages_list_user_data *user_data, const char *name)
{
  di_package *p = di_packages_get_package (user_data->packages, name, 0);
  if (p)
    di_tree_insert (user_data->include, p->package, p);
  else
    log_text (DI_LOG_LEVEL_WARNING, "Can't find package %s for inclusion", name);
}

static void suite_packages_list_sections (void *key __attribute__ ((unused)), void *data, void *_user_data)
{
  suite_config_section *section = data;
  struct suite_packages_list_user_data *user_data = _user_data;
  di_slist_node *node1, *node2;

  if (section->activate)
    for (node1 = section->packages.head; node1; node1 = node1->next)
    {
      suite_config_packages *packages = node1->data;

      if (packages->activate)
        for (node2 = packages->packages.head; node2; node2 = node2->next)
        {
          const char *name = node2->data;
          if (!strcmp (name, "priority-required"))
            user_data->select_priority_required = true;
          else if (!strcmp (name, "priority-important"))
            user_data->select_priority_important = true;
          else if (name[0] == '-')
            suite_packages_list_add_exclude (user_data, &name[1]);
          else
            suite_packages_list_add_include (user_data, name);
        }
    }
}

static void suite_packages_list_packages (void *key __attribute__ ((unused)), void *data, void *_user_data)
{
  di_package *p = data;
  struct suite_packages_list_user_data *user_data = _user_data;

  if (p->essential ||
      (user_data->select_priority_required && p->priority == di_package_priority_required) ||
      (user_data->select_priority_important && p->priority == di_package_priority_important))
    /* These packages will automatically be installed */
    di_tree_insert (user_data->include, p->package, p);
}

static void suite_packages_list_process (void *key, void *data, void *_user_data)
{
  struct suite_packages_list_user_data *user_data = _user_data;

  if (!di_tree_lookup (user_data->exclude, key))
    di_slist_append (&user_data->list, data);
}

di_slist *suite_packages_list (di_packages *packages, di_packages_allocator *allocator)
{
  di_slist *install;
  struct suite_packages_list_user_data user_data = { NULL, NULL, { NULL, NULL }, packages, false, false };

  user_data.include = di_tree_new (suite_packages_list_cmp);
  user_data.exclude = di_tree_new (suite_packages_list_cmp);

  di_hash_table_foreach (suite->sections, suite_packages_list_sections, &user_data);
  di_hash_table_foreach (packages->table, suite_packages_list_packages, &user_data);

  if (include)
    for (di_slist_node *node = include->head; node; node = node->next)
      suite_packages_list_add_include (&user_data, node->data);
  if (exclude)
    for (di_slist_node *node = exclude->head; node; node = node->next)
      suite_packages_list_add_exclude (&user_data, node->data);

  di_tree_foreach (user_data.include, suite_packages_list_process, &user_data);

  install = di_packages_resolve_dependencies (packages, &user_data.list, allocator);

  di_slist_destroy (&user_data.list, NULL);
  di_tree_destroy (user_data.include);
  di_tree_destroy (user_data.exclude);

  return install;
}

