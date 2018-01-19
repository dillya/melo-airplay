/*
 * melo_airplay_plugin.c: Airplay plugin for Melo
 *
 * Copyright (C) 2016 Alexandre Dilly <dillya@sparod.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <melo_plugin.h>
#include <melo_module.h>

#include "melo_airplay.h"

static gboolean
melo_airplay_enable (void)
{
  return melo_module_register (MELO_TYPE_AIRPLAY, "airplay");
}

static gboolean
melo_airplay_disable (void)
{
  melo_module_unregister ("airplay");
  return TRUE;
}

DECLARE_MELO_PLUGIN ("Airplay",
                     "Airplay support for Melo",
                     melo_airplay_enable,
                     melo_airplay_disable);
