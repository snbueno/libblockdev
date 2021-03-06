/*
 * Copyright (C) 2014  Red Hat, Inc.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <glib.h>
#include <string.h>
#include <unistd.h>
#include <utils.h>

#include "btrfs.h"

/**
 * SECTION: btrfs
 * @short_description: plugin for operations with BTRFS devices
 * @title: BTRFS
 * @include: btrfs.h
 *
 * A plugin for operations with btrfs devices.
 */

/**
 * bd_btrfs_error_quark: (skip)
 */
GQuark bd_btrfs_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-btrfs-error-quark");
}

BDBtrfsDeviceInfo* bd_btrfs_device_info_copy (BDBtrfsDeviceInfo *info) {
    BDBtrfsDeviceInfo *new_info = g_new (BDBtrfsDeviceInfo, 1);

    new_info->id = info->id;
    new_info->path = g_strdup (info->path);
    new_info->size = info->size;
    new_info->used = info->used;

    return new_info;
}

void bd_btrfs_device_info_free (BDBtrfsDeviceInfo *info) {
    g_free (info->path);
    g_free (info);
}

BDBtrfsSubvolumeInfo* bd_btrfs_subvolume_info_copy (BDBtrfsSubvolumeInfo *info) {
    BDBtrfsSubvolumeInfo *new_info = g_new (BDBtrfsSubvolumeInfo, 1);

    new_info->id = info->id;
    new_info->parent_id = info->parent_id;
    new_info->path = g_strdup (info->path);

    return new_info;
}

void bd_btrfs_subvolume_info_free (BDBtrfsSubvolumeInfo *info) {
    g_free (info->path);
    g_free (info);
}

BDBtrfsFilesystemInfo* bd_btrfs_filesystem_info_copy (BDBtrfsFilesystemInfo *info) {
    BDBtrfsFilesystemInfo *new_info = g_new (BDBtrfsFilesystemInfo, 1);

    new_info->label = g_strdup (info->label);
    new_info->uuid = g_strdup (info->uuid);
    new_info->num_devices = info->num_devices;
    new_info->used = info->used;

    return new_info;
}

void bd_btrfs_filesystem_info_free (BDBtrfsFilesystemInfo *info) {
    g_free (info->label);
    g_free (info->uuid);
    g_free (info);
}

static BDBtrfsDeviceInfo* get_device_info_from_match (GMatchInfo *match_info) {
    BDBtrfsDeviceInfo *ret = g_new(BDBtrfsDeviceInfo, 1);
    gchar *item = NULL;
    GError *error = NULL;

    item = g_match_info_fetch_named (match_info, "id");
    ret->id = g_ascii_strtoull (item, NULL, 0);
    g_free (item);

    ret->path = g_match_info_fetch_named (match_info, "path");

    item = g_match_info_fetch_named (match_info, "size");
    ret->size = bd_utils_size_from_spec (item, &error);
    g_free (item);
    if (error)
        g_warning ("%s", error->message);
    g_clear_error (&error);

    item = g_match_info_fetch_named (match_info, "used");
    ret->used = bd_utils_size_from_spec (item, &error);
    g_free (item);
    if (error)
        g_warning ("%s", error->message);
    g_clear_error (&error);

    return ret;
}

static BDBtrfsSubvolumeInfo* get_subvolume_info_from_match (GMatchInfo *match_info) {
    BDBtrfsSubvolumeInfo *ret = g_new(BDBtrfsSubvolumeInfo, 1);
    gchar *item = NULL;

    item = g_match_info_fetch_named (match_info, "id");
    ret->id = g_ascii_strtoull (item, NULL, 0);
    g_free (item);

    item = g_match_info_fetch_named (match_info, "parent_id");
    ret->parent_id = g_ascii_strtoull (item, NULL, 0);
    g_free (item);

    ret->path = g_match_info_fetch_named (match_info, "path");

    return ret;
}

static BDBtrfsFilesystemInfo* get_filesystem_info_from_match (GMatchInfo *match_info) {
    BDBtrfsFilesystemInfo *ret = g_new(BDBtrfsFilesystemInfo, 1);
    gchar *item = NULL;
    GError *error = NULL;

    ret->label = g_match_info_fetch_named (match_info, "label");
    ret->uuid = g_match_info_fetch_named (match_info, "uuid");

    item = g_match_info_fetch_named (match_info, "num_devices");
    ret->num_devices = g_ascii_strtoull (item, NULL, 0);
    g_free (item);

    item = g_match_info_fetch_named (match_info, "used");
    ret->used = bd_utils_size_from_spec (item, &error);
    g_free (item);
    if (error)
        g_warning ("%s", error->message);
    g_clear_error (&error);

    return ret;
}

/**
 * bd_btrfs_create_volume:
 * @devices: (array zero-terminated=1): list of devices to create btrfs volume from
 * @label: (allow-none): label for the volume
 * @data_level: (allow-none): RAID level for the data or %NULL to use the default
 * @md_level: (allow-none): RAID level for the metadata or %NULL to use the default
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the new btrfs volume was created from @devices or not
 *
 * See mkfs.btrfs(8) for details about @data_level, @md_level and btrfs in general.
 */
gboolean bd_btrfs_create_volume (gchar **devices, gchar *label, gchar *data_level, gchar *md_level, GError **error) {
    gchar **device_p = NULL;
    guint8 num_args = 0;
    gchar **argv = NULL;
    guint8 next_arg = 1;
    gboolean success = FALSE;

    if (!devices || (g_strv_length (devices) < 1)) {
        g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_DEVICE, "No devices given");
        return FALSE;
    }

    for (device_p = devices; *device_p != NULL; device_p++) {
        if (access (*device_p, F_OK) != 0) {
            g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_DEVICE, "Device %s does not exist", *device_p);
            return FALSE;
        }
        num_args++;
    }

    if (label)
        num_args += 2;
    if (data_level)
        num_args += 2;
    if (md_level)
        num_args += 2;

    argv = g_new (gchar*, num_args + 2);
    argv[0] = "mkfs.btrfs";
    if (label) {
        argv[next_arg] = "--label";
        next_arg++;
        argv[next_arg] = label;
        next_arg++;
    }
    if (data_level) {
        argv[next_arg] = "--data";
        next_arg++;
        argv[next_arg] = data_level;
        next_arg++;
    }
    if (md_level) {
        argv[next_arg] = "--metadata";
        next_arg++;
        argv[next_arg] = md_level;
        next_arg++;
    }

    for (device_p = devices; next_arg <= num_args; device_p++, next_arg++)
        argv[next_arg] = *device_p;
    argv[next_arg] = NULL;

    success = bd_utils_exec_and_report_error (argv, error);
    g_free (argv);
    return success;
}

/**
 * bd_btrfs_add_device:
 * @mountpoint: mountpoint of the btrfs volume to add new device to
 * @device: a device to add to the btrfs volume
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully added to the @mountpoint btrfs volume or not
 */
gboolean bd_btrfs_add_device (gchar *mountpoint, gchar *device, GError **error) {
    gchar *argv[6] = {"btrfs", "device", "add", device, mountpoint, NULL};
    return bd_utils_exec_and_report_error (argv, error);
}

/**
 * bd_btrfs_remove_device:
 * @mountpoint: mountpoint of the btrfs volume to remove device from
 * @device: a device to remove from the btrfs volume
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully removed from the @mountpoint btrfs volume or not
 */
gboolean bd_btrfs_remove_device (gchar *mountpoint, gchar *device, GError **error) {
    gchar *argv[6] = {"btrfs", "device", "delete", device, mountpoint, NULL};
    return bd_utils_exec_and_report_error (argv, error);
}

/**
 * bd_btrfs_create_subvolume:
 * @mountpoint: mountpoint of the btrfs volume to create subvolume under
 * @name: name of the subvolume
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @mountpoint/@name subvolume was successfully created or not
 */
gboolean bd_btrfs_create_subvolume (gchar *mountpoint, gchar *name, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    gchar *argv[5] = {"btrfs", "subvol", "create", NULL, NULL};

    if (g_str_has_suffix (mountpoint, "/"))
        path = g_strdup_printf ("%s%s", mountpoint, name);
    else
        path = g_strdup_printf ("%s/%s", mountpoint, name);
    argv[3] = path;

    success = bd_utils_exec_and_report_error (argv, error);
    g_free (path);

    return success;
}

/**
 * bd_btrfs_delete_subvolume:
 * @mountpoint: mountpoint of the btrfs volume to delete subvolume from
 * @name: name of the subvolume
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @mountpoint/@name subvolume was successfully deleted or not
 */
gboolean bd_btrfs_delete_subvolume (gchar *mountpoint, gchar *name, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    gchar *argv[5] = {"btrfs", "subvol", "delete", NULL, NULL};

    if (g_str_has_suffix (mountpoint, "/"))
        path = g_strdup_printf ("%s%s", mountpoint, name);
    else
        path = g_strdup_printf ("%s/%s", mountpoint, name);
    argv[3] = path;

    success = bd_utils_exec_and_report_error (argv, error);
    g_free (path);

    return success;
}

/**
 * bd_btrfs_get_default_subvolume_id:
 * @mountpoint: mountpoint of the volume to get the default subvolume ID of
 * @error: (out): place to store error (if any)
 *
 * Returns: ID of the @mountpoint volume's default subvolume. If 0,
 * @error) may be set to indicate error
 */
guint64 bd_btrfs_get_default_subvolume_id (gchar *mountpoint, GError **error) {
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar *match = NULL;
    guint64 ret = 0;
    gchar *argv[5] = {"btrfs", "subvol", "get-default", mountpoint, NULL};

    regex = g_regex_new ("ID (\\d+) .*", 0, 0, error);
    if (!regex) {
        g_warning ("Failed to create new GRegex");
        /* error is already populated */
        return 0;
    }

    success = bd_utils_exec_and_capture_output (argv, &output, error);
    if (!success) {
        g_regex_unref (regex);
        return 0;
    }

    success = g_regex_match (regex, output, 0, &match_info);
    if (!success) {
        g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_PARSE, "Failed to parse subvolume's ID");
        g_regex_unref (regex);
        g_match_info_free (match_info);
        g_free (output);
        return 0;
    }

    match = g_match_info_fetch (match_info, 1);
    ret = g_ascii_strtoull (match, NULL, 0);

    g_free (match);
    g_match_info_free (match_info);
    g_regex_unref (regex);
    g_free (output);

    return ret;
}

/**
 * bd_btrfs_set_default_subvolume:
 * @mountpoint: mountpoint of the volume to set the default subvolume ID of
 * @subvol_id: ID of the subvolume to be set as the default subvolume
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @mountpoint volume's default subvolume was correctly set
 * to @subvol_id or not
 */
gboolean bd_btrfs_set_default_subvolume (gchar *mountpoint, guint64 subvol_id, GError **error) {
    gchar *argv[6] = {"btrfs", "subvol", "set-default", NULL, mountpoint, NULL};
    gboolean ret = FALSE;

    argv[3] = g_strdup_printf ("%"G_GUINT64_FORMAT, subvol_id);
    ret = bd_utils_exec_and_report_error (argv, error);
    g_free (argv[3]);

    return ret;
}

/**
 * bd_btrfs_create_snapshot:
 * @source: path to source subvolume
 * @dest: path to new snapshot volume
 * @ro: whether the snapshot should be read-only
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @dest snapshot of @source was successfully created or not
 */
gboolean bd_btrfs_create_snapshot (gchar *source, gchar *dest, gboolean ro, GError **error) {
    gchar *argv[7] = {"btrfs", "subvol", "snapshot", NULL, NULL, NULL, NULL};
    guint next_arg = 3;

    if (ro) {
        argv[next_arg] = "-r";
        next_arg++;
    }
    argv[next_arg] = source;
    next_arg++;
    argv[next_arg] = dest;

    return bd_utils_exec_and_report_error (argv, error);
}

/**
 * bd_btrfs_list_devices:
 * @device: a device that is part of the queried btrfs volume
 * @error: (out): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about the devices that are part of the btrfs volume
 * containing @device or %NULL in case of error
 */
BDBtrfsDeviceInfo** bd_btrfs_list_devices (gchar *device, GError **error) {
    gchar *argv[5] = {"btrfs", "filesystem", "show", device, NULL};
    gchar *output = NULL;
    gboolean success = FALSE;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar const * const pattern = "devid[ \\t]+(?P<id>\\d+)[ \\t]+" \
                                  "size[ \\t]+(?P<size>\\S+)[ \\t]+" \
                                  "used[ \\t]+(?P<used>\\S+)[ \\t]+" \
                                  "path[ \\t]+(?P<path>\\S+)\n";
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    guint8 i = 0;
    GPtrArray *dev_infos = g_ptr_array_new ();
    BDBtrfsDeviceInfo** ret = NULL;

    regex = g_regex_new (pattern, G_REGEX_EXTENDED, 0, error);
    if (!regex) {
        g_warning ("Failed to create new GRegex");
        /* error is already populated */
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (argv, &output, error);
    if (!success)
        /* error is already populated from the previous call */
        return NULL;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (line_p = lines; *line_p; line_p++) {
        success = g_regex_match (regex, *line_p, 0, &match_info);
        if (!success) {
            g_match_info_free (match_info);
            continue;
        }

        g_ptr_array_add (dev_infos, get_device_info_from_match (match_info));
        g_match_info_free (match_info);
    }

    g_strfreev (lines);

    if (dev_infos->len == 0) {
        g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_PARSE, "Failed to parse information about devices");
        return NULL;
    }

    /* now create the return value -- NULL-terminated array of BDBtrfsDeviceInfo */
    ret = g_new (BDBtrfsDeviceInfo*, dev_infos->len + 1);
    for (i=0; i < dev_infos->len; i++)
        ret[i] = (BDBtrfsDeviceInfo*) g_ptr_array_index (dev_infos, i);
    ret[i] = NULL;

    g_ptr_array_free (dev_infos, FALSE);

    return ret;
}

/**
 * bd_btrfs_list_subvolumes:
 * @mountpoint: a mountpoint of the queried btrfs volume
 * @snapshots_only: whether to list only snapshot subvolumes or not
 * @error: (out): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about the subvolumes that are part of the btrfs volume
 * mounted at @mountpoint or %NULL in case of error
 */
BDBtrfsSubvolumeInfo** bd_btrfs_list_subvolumes (gchar *mountpoint, gboolean snapshots_only, GError **error) {
    gchar *argv[7] = {"btrfs", "subvol", "list", "-p", NULL, NULL, NULL};
    gchar *output = NULL;
    gboolean success = FALSE;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar const * const pattern = "ID\\s+(?P<id>\\d+)\\s+gen\\s+\\d+\\s+(cgen\\s+\\d+\\s+)?" \
                                  "parent\\s+(?P<parent_id>\\d+)\\s+top\\s+level\\s+\\d+\\s+" \
                                  "(otime\\s+\\d{4}-\\d{2}-\\d{2}\\s+\\d\\d:\\d\\d:\\d\\d\\s+)?"\
                                  "path\\s+(?P<path>\\S+)";
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    guint8 i = 0;
    GPtrArray *subvol_infos = g_ptr_array_new ();
    BDBtrfsSubvolumeInfo** ret = NULL;

    if (snapshots_only) {
        argv[4] = "-s";
        argv[5] = mountpoint;
    } else
        argv[4] = mountpoint;

    regex = g_regex_new (pattern, G_REGEX_EXTENDED, 0, error);
    if (!regex) {
        g_warning ("Failed to create new GRegex");
        /* error is already populated */
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (argv, &output, error);
    if (!success)
        /* error is already populated from the call above or simply no output*/
        return NULL;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (line_p = lines; *line_p; line_p++) {
        success = g_regex_match (regex, *line_p, 0, &match_info);
        if (!success) {
            g_match_info_free (match_info);
            continue;
        }

        g_ptr_array_add (subvol_infos, get_subvolume_info_from_match (match_info));
        g_match_info_free (match_info);
    }

    g_strfreev (lines);

    if (subvol_infos->len == 0) {
        g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_PARSE, "Failed to parse information about subvolumes");
        return NULL;
    }

    /* now create the return value -- NULL-terminated array of BDBtrfsSubvolumeInfo */
    ret = g_new (BDBtrfsSubvolumeInfo*, subvol_infos->len + 1);
    for (i=0; i < subvol_infos->len; i++)
        ret[i] = (BDBtrfsSubvolumeInfo*) g_ptr_array_index (subvol_infos, i);
    ret[i] = NULL;

    g_ptr_array_free (subvol_infos, FALSE);

    return ret;
}

/**
 * bd_btrfs_filesystem_info:
 * @device: a device that is part of the queried btrfs volume
 * @error: (out): place to store error (if any)
 *
 * Returns: information about the @device's volume's filesystem or %NULL in case of error
 */
BDBtrfsFilesystemInfo* bd_btrfs_filesystem_info (gchar *device, GError **error) {
    gchar *argv[5] = {"btrfs", "filesystem", "show", device, NULL};
    gchar *output = NULL;
    gboolean success = FALSE;
    gchar const * const pattern = "Label:\\s+'(?P<label>\\S+)'\\s+" \
                                  "uuid:\\s+(?P<uuid>\\S+)\\s+" \
                                  "Total\\sdevices\\s+(?P<num_devices>\\d+)\\s+" \
                                  "FS\\sbytes\\sused\\s+(?P<used>\\S+)";
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    BDBtrfsFilesystemInfo *ret = NULL;

    regex = g_regex_new (pattern, G_REGEX_EXTENDED, 0, error);
    if (!regex) {
        g_warning ("Failed to create new GRegex");
        /* error is already populated */
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (argv, &output, error);
    if (!success)
        /* error is already populated from the call above or just empty
           output */
        return NULL;

    success = g_regex_match (regex, output, 0, &match_info);
    if (!success) {
        g_regex_unref (regex);
        g_match_info_free (match_info);
        return NULL;
    }

    g_regex_unref (regex);
    ret = get_filesystem_info_from_match (match_info);
    g_match_info_free (match_info);

    return ret;
}

/**
 * bd_btrfs_mkfs:
 * @devices: (array zero-terminated=1): list of devices to create btrfs volume from
 * @label: (allow-none): label for the volume
 * @data_level: (allow-none): RAID level for the data or %NULL to use the default
 * @md_level: (allow-none): RAID level for the metadata or %NULL to use the default
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the new btrfs volume was created from @devices or not
 *
 * See mkfs.btrfs(8) for details about @data_level, @md_level and btrfs in general.
 */
gboolean bd_btrfs_mkfs (gchar **devices, gchar *label, gchar *data_level, gchar *md_level, GError **error) {
    return bd_btrfs_create_volume (devices, label, data_level, md_level, error);
}

/**
 * bd_btrfs_resize:
 * @mountpoint: a mountpoint of the to be resized btrfs filesystem
 * @size: requested new size
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @mountpoint filesystem was successfully resized to @size
 * or not
 */
gboolean bd_btrfs_resize (gchar *mountpoint, guint64 size, GError **error) {
    gchar *argv[6] = {"btrfs", "filesystem", "resize", NULL, mountpoint, NULL};
    gboolean ret = FALSE;

    argv[3] = g_strdup_printf ("%"G_GUINT64_FORMAT, size);
    ret = bd_utils_exec_and_report_error (argv, error);
    g_free (argv[3]);

    return ret;
}

/**
 * bd_btrfs_check:
 * @device: a device that is part of the checked btrfs volume
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the filesystem was successfully checked or not
 */
gboolean bd_btrfs_check (gchar *device, GError **error) {
    gchar *argv[4] = {"btrfs", "check", device, NULL};

    return bd_utils_exec_and_report_error (argv, error);
}

/**
 * bd_btrfs_repair:
 * @device: a device that is part of the to be repaired btrfs volume
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the filesystem was successfully checked and repaired or not
 */
gboolean bd_btrfs_repair (gchar *device, GError **error) {
    gchar *argv[5] = {"btrfs", "check", "--repair", device, NULL};

    return bd_utils_exec_and_report_error (argv, error);
}

/**
 * bd_btrfs_change_label:
 * @mountpoint: a mountpoint of the btrfs filesystem to change label of
 * @label: new label for the filesystem
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the label of the @mountpoint filesystem was successfully set
 * to @label or not
 */
gboolean bd_btrfs_change_label (gchar *mountpoint, gchar *label, GError **error) {
    gchar *argv[6] = {"btrfs", "filesystem", "label", mountpoint, label, NULL};

    return bd_utils_exec_and_report_error (argv, error);
}
