/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control modem status & access information from the command line
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 * Copyright (C) 2011 Google, Inc.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include "libmm-glib.h"

#include "mmcli.h"

/* Context */
typedef struct {
    MMManager *manager;
    GCancellable *cancellable;
} Context;
static Context *ctx;

/* Options */
static gboolean list_modems_flag;
static gboolean monitor_modems_flag;
static gboolean scan_modems_flag;
static gchar *set_logging_str;

static GOptionEntry entries[] = {
    { "set-logging", 'G', 0, G_OPTION_ARG_STRING, &set_logging_str,
      "Set logging level in the ModemManager daemon",
      "[ERR,WARN,INFO,DEBUG]",
    },
    { "list-modems", 'L', 0, G_OPTION_ARG_NONE, &list_modems_flag,
      "List available modems",
      NULL
    },
    { "monitor-modems", 'M', 0, G_OPTION_ARG_NONE, &monitor_modems_flag,
      "List available modems and monitor additions and removals",
      NULL
    },
    { "scan-modems", 'S', 0, G_OPTION_ARG_NONE, &scan_modems_flag,
      "Request to re-scan looking for modems",
      NULL
    },
    { NULL }
};

GOptionGroup *
mmcli_manager_get_option_group (void)
{
	GOptionGroup *group;

	/* Status options */
	group = g_option_group_new ("manager",
	                            "Manager options",
	                            "Show manager options",
	                            NULL,
	                            NULL);
	g_option_group_add_entries (group, entries);

	return group;
}

gboolean
mmcli_manager_options_enabled (void)
{
    guint n_actions;

    n_actions = (list_modems_flag +
                 monitor_modems_flag +
                 scan_modems_flag +
                 (set_logging_str ? 1 : 0));

    if (n_actions > 1) {
        g_printerr ("error: too many manager actions requested\n");
        exit (EXIT_FAILURE);
    }

    return !!n_actions;
}

static void
context_free (Context *ctx)
{
    if (!ctx)
        return;

    if (ctx->manager)
        g_object_unref (ctx->manager);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_free (ctx);
}

void
mmcli_manager_shutdown (void)
{
    context_free (ctx);
}

static void
set_logging_process_reply (gboolean      result,
                           const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't set logging level: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("Successfully set logging level\n");
}

static void
set_logging_ready (MMManager    *manager,
                   GAsyncResult *result,
                   gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_manager_set_logging_finish (manager,
                                                      result,
                                                      &error);
    set_logging_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
scan_devices_process_reply (gboolean      result,
                            const GError *error)
{
    if (!result) {
        g_printerr ("error: couldn't request to scan devices: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully requested to scan devices\n");
}

static void
scan_devices_ready (MMManager    *manager,
                    GAsyncResult *result,
                    gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_manager_scan_devices_finish (manager,
                                                       result,
                                                       &error);
    scan_devices_process_reply (operation_result, error);

    mmcli_async_operation_done ();
}

static void
device_added (GDBusObjectManager *manager,
              GDBusObject        *object)
{
    g_print ("Added modem [TODO: Print path]\n");
    fflush (stdout);
}

static void
device_removed (GDBusObjectManager *manager,
                GDBusObject        *object)
{
    g_print ("Removed modem [TODO: Print path]\n");
    fflush (stdout);
}

static void
list_current_modems (MMManager *manager)
{
    GList *modems;

    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (ctx->manager));

    g_print ("\n");
    if (!modems)
        g_print ("No modems were found\n");
    else {
        GList *l;

        g_print ("Found %u modems\n", g_list_length (modems));
        for (l = modems; l; l = g_list_next (l)) {
            g_print ("\t[TODO: Print path]\n");
            g_object_unref (l->data);
        }
        g_list_free (modems);
    }
}

static void
cancelled (GCancellable *cancellable)
{
    mmcli_async_operation_done ();
}

static void
manager_new_ready (GObject      *source,
                   GAsyncResult *result,
                   gpointer      none)
{
    gchar *name_owner;
    GError *error = NULL;

    ctx->manager = mm_manager_new_finish (result, &error);
    if (!ctx->manager) {
        g_printerr ("error: couldn't create manager: %s\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    name_owner = g_dbus_object_manager_client_get_name_owner (G_DBUS_OBJECT_MANAGER_CLIENT (ctx->manager));
    if (!name_owner) {
        g_printerr ("error: couldn't find the ModemManager process in the bus\n");
        exit (EXIT_FAILURE);
    }

    g_debug ("ModemManager process found at '%s'", name_owner);
    g_free (name_owner);

    /* Request to set log level? */
    if (set_logging_str) {
        mm_manager_set_logging (ctx->manager,
                                set_logging_str,
                                ctx->cancellable,
                                (GAsyncReadyCallback)set_logging_ready,
                                NULL);
        return;
    }

    /* Request to scan modems? */
    if (scan_modems_flag) {
        mm_manager_scan_devices (ctx->manager,
                                 ctx->cancellable,
                                 (GAsyncReadyCallback)scan_devices_ready,
                                 NULL);
        return;
    }

    /* Request to monitor modems? */
    if (monitor_modems_flag) {
        g_signal_connect (ctx->manager,
                          "object-added",
                          G_CALLBACK (device_added),
                          NULL);
        g_signal_connect (ctx->manager,
                          "object-removed",
                          G_CALLBACK (device_removed),
                          NULL);
        list_current_modems (ctx->manager);

        /* If we get cancelled, operation done */
        g_cancellable_connect (ctx->cancellable,
                               G_CALLBACK (cancelled),
                               NULL,
                               NULL);
        return;
    }

    /* Request to list modems? */
    if (list_modems_flag) {
        list_current_modems (ctx->manager);
        mmcli_async_operation_done ();
        return;
    }

    g_warn_if_reached ();
}

void
mmcli_manager_run_asynchronous (GDBusConnection *connection,
                                GCancellable    *cancellable)
{
    /* Initialize context */
    ctx = g_new0 (Context, 1);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);

    /* Create a new Manager object asynchronously */
    mm_manager_new (connection,
                    G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                    cancellable,
                    (GAsyncReadyCallback)manager_new_ready,
                    NULL);
}

void
mmcli_manager_run_synchronous (GDBusConnection *connection)
{
    gchar *name_owner;
    GError *error = NULL;

    if (monitor_modems_flag) {
        g_printerr ("error: monitoring modems cannot be done synchronously\n");
        exit (EXIT_FAILURE);
    }

    /* Initialize context */
    ctx = g_new0 (Context, 1);
    ctx->manager = mm_manager_new_sync (connection,
                                        G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                        NULL,
                                        &error);
    if (!ctx->manager) {
        g_printerr ("error: couldn't create manager: %s\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    name_owner = g_dbus_object_manager_client_get_name_owner (G_DBUS_OBJECT_MANAGER_CLIENT (ctx->manager));
    if (!name_owner) {
        g_printerr ("error: couldn't find the ModemManager process in the bus\n");
        exit (EXIT_FAILURE);
    }

    g_debug ("ModemManager process found at '%s'", name_owner);
    g_free (name_owner);

    /* Request to set log level? */
    if (set_logging_str) {
        gboolean result;

        result = mm_manager_set_logging_sync (ctx->manager,
                                              set_logging_str,
                                              NULL,
                                              &error);
        set_logging_process_reply (result, error);
        return;
    }

    /* Request to scan modems? */
    if (scan_modems_flag) {
        gboolean result;

        result = mm_manager_scan_devices_sync (ctx->manager,
                                               NULL,
                                               &error);
        scan_devices_process_reply (result, error);
        return;
    }

    /* Request to list modems? */
    if (list_modems_flag) {
        list_current_modems (ctx->manager);
        return;
    }

    g_warn_if_reached ();
}