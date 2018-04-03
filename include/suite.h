/*
 * suite.h
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

#ifndef SUITE_H
#define SUITE_H

#include <debian-installer.h>
#include <stdbool.h>

#include "package.h"
#include "suite_config.h"

extern struct suites_config_suite *suites_suite;
extern struct suite_config *suite;
extern const char *arch;
extern const char *flavour;
extern const char *suite_name;
extern bool authentication;

int suite_init (const char *suite_name, const char *suite_config_name, const char *arch, const char *flavour, di_slist *include, di_slist *exclude, const char *configdir, bool authentication);
int suite_install (di_packages *packages, di_packages_allocator *allocator, di_slist *install);
di_slist *suite_packages_list (di_packages *packages, di_packages_allocator *allocator);
int suite_use (const char *suite_name);

#endif
