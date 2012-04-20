/*
 * Copyright 2011 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <edify/expr.h>
#include <updater/updater.h>

#include "update_osip.h"
#include "util.h"
#include "modem_fw.h"
#include "fw_version_check.h"
#include "flash_ifwi.h"

Value *ExtractOsipFn(const char *name, State *state, int argc, Expr *argv[]) {
    Value *ret = NULL;
    char *filename = NULL;
    char *source = NULL;
    int osii_index;
    void *data = NULL;
    size_t size;

    if (ReadArgs(state, argv, 2, &filename, &source) < 0) {
        return NULL;
    }

    if (strlen(filename) == 0) {
        ErrorAbort(state, "filename argument to %s can't be empty", name);
        goto done;
    }

    if (strlen(source) == 0) {
        ErrorAbort(state, "source argument to %s can't be empty", name);
        goto done;
    }

    osii_index = get_named_osii_index(source);
    if (osii_index < 0) {
        ErrorAbort(state, "Can't get OSII index for %s", source);
        goto done;
    }

    if (read_osimage_data(&data, &size, osii_index) < 0) {
        ErrorAbort(state, "Couldn't read osip[%d]", osii_index);
        goto done;
    }

    if (file_write(filename, data, size) < 0) {
        ErrorAbort(state, "Couldn't write osii[%d] data to %s", osii_index,
                filename);
        goto done;
    }

    ret = StringValue(strdup(""));
done:
    if (source)
        free(source);
    if (filename)
        free(filename);
    if (data)
        free(data);

    return ret;
}

Value *FlashOsipFn(const char *name, State *state, int argc, Expr *argv[]) {
    Value *ret = NULL;
    char *filename = NULL;
    char *destination = NULL;
    int osii_index;
    void *image_data = NULL;
    size_t image_size;

    if (ReadArgs(state, argv, 2, &filename, &destination) < 0) {
        return NULL;
    }

    if (strlen(filename) == 0) {
        ErrorAbort(state, "filename argument to %s can't be empty", name);
        goto done;
    }

    if (strlen(destination) == 0) {
        ErrorAbort(state, "destination argument to %s can't be empty", name);
        goto done;
    }

    if (file_read(filename, &image_data, &image_size)) {
        ErrorAbort(state, "Cannot open os image %s", filename);
        goto done;
    }

    osii_index = get_named_osii_index(destination);

    if (osii_index < 0) {
        ErrorAbort(state, "Can't get OSII index for %s", destination);
        goto done;
    }

    if (write_stitch_image(image_data, image_size, osii_index)) {
        ErrorAbort(state, "Error writing %s image %s to OSIP%d",
                destination, filename, osii_index);
        goto done;
    }

    ret = StringValue(strdup(""));
done:
    if (image_data)
        free(image_data);
    if (destination)
        free(destination);
    if (filename)
        free(filename);

    return ret;
}

#define DNX_BIN_PATH	"/tmp/dnx.bin"
#define IFWI_BIN_PATH	"/tmp/ifwi.bin"
#define IFWI_NAME	"ifwi"
#define DNX_NAME	"dnx"
#define FILEMODE  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH

Value *FlashIfwiFn(const char *name, State *state, int argc, Expr *argv[]) {
    Value *ret = NULL;
    char *filename = NULL;
    int err, ifwi_bin_fd, dnx_bin_fd, i, num;
    char ifwi_name[128], dnx_name[128];
    ZipArchive ifwi_za;
    const ZipEntry *dnx_entry, *ifwi_entry;

    if (ReadArgs(state, argv, 1, &filename) < 0) {
        return NULL;
    }

    if (strlen(filename) == 0) {
        ErrorAbort(state, "filename argument to %s can't be empty", name);
        goto done;
    }

    err = mzOpenZipArchive(filename, &ifwi_za);
    if (err) {
        ErrorAbort(state, "Failed to open zip archive %s\n", filename);
        goto done;
    }

    num = mzZipEntryCount(&ifwi_za);
    for (i = 0; i < num; i++) {
        ifwi_entry = mzGetZipEntryAt(&ifwi_za, i);
        if ((ifwi_entry->fileNameLen + 1) < sizeof(ifwi_name)){
            strncpy(ifwi_name, ifwi_entry->fileName, ifwi_entry->fileNameLen);
            ifwi_name[ifwi_entry->fileNameLen] = '\0';
        } else {
            ErrorAbort(state, "ifwi file name is too big size max :%d.\n", sizeof(ifwi_name));
            goto done;
        }
        if (strncmp(ifwi_name, IFWI_NAME, strlen(IFWI_NAME)))
            continue;

        if ((ifwi_bin_fd = open(IFWI_BIN_PATH, O_RDWR | O_TRUNC | O_CREAT, FILEMODE)) < 0) {
            ErrorAbort(state, "unable to creat Extracted file:%s.\n", IFWI_BIN_PATH);
            goto done;
        }
        if ((dnx_bin_fd = open(DNX_BIN_PATH, O_RDWR | O_TRUNC | O_CREAT, FILEMODE)) < 0) {
            ErrorAbort(state, "unable to creat Extracted file:%s.\n", IFWI_BIN_PATH);
            goto done;
        }
        strcpy(dnx_name, "dnx");
        strncat(dnx_name, &(ifwi_name[strlen(IFWI_NAME)]), sizeof(dnx_name) - strlen("dnx") -1);
        dnx_entry = mzFindZipEntry(&ifwi_za, dnx_name);

        err = mzExtractZipEntryToFile(&ifwi_za, dnx_entry, dnx_bin_fd);
        if (!err) {
            ErrorAbort(state, "Failed to unzip %s\n", DNX_BIN_PATH);
            goto done;
        }
        close(dnx_bin_fd);
        err = mzExtractZipEntryToFile(&ifwi_za, ifwi_entry, ifwi_bin_fd);
        if (!err) {
            ErrorAbort(state, "Failed to unzip %s\n", DNX_BIN_PATH);
            goto done;
        }
        close(ifwi_bin_fd);
        update_ifwi_file(DNX_BIN_PATH, IFWI_BIN_PATH);
    }
    mzCloseZipArchive(&ifwi_za);

    ret = StringValue(strdup(""));
done:
    if (filename)
        free(filename);

    return ret;
}

static void progress_callback(enum cmfwdl_status_type type, int value,
        const char *msg, void *data)
{
    static int last_update_progress = -1;

    switch (type) {
    case cmfwdl_status_booting:
        printf("modem: Booting...\n");
        last_update_progress = -1;
        break;
    case cmfwdl_status_synced:
        printf("modem: Device Synchronized\n");
        last_update_progress = -1;
        break;
    case cmfwdl_status_downloading:
        printf("modem: Loading Component %s\n", msg);
        last_update_progress = -1;
        break;
    case cmfwdl_status_msg_detail:
        printf("modem: %s\n", msg);
        last_update_progress = -1;
        break;
    case cmfwdl_status_error_detail:
        printf("modem: ERROR: %s\n", msg);
        last_update_progress = -1;
        break;
    case cmfwdl_status_progress:
        if (value / 10 == last_update_progress)
            break;
        last_update_progress = value / 10;
        printf("modem: update progress %d%%\n", last_update_progress);
        break;
    case cmfwdl_status_version:
        printf("modem: Version: %s\n", msg);
        break;
    default:
        printf("modem: Ignoring: %s\n", msg);
        break;
    }
}

Value *FlashModemFn(const char *name, State *state, int argc, Expr *argv[]) {
    Value *ret = NULL;
    char *filename = NULL;
    char *argvmodem[] = {"f"};

    if (ReadArgs(state, argv, 1, &filename) < 0) {
        return NULL;
    }

    if (strlen(filename) == 0) {
        ErrorAbort(state, "filename argument to %s can't be empty", name);
        goto done;
    }

    if (flash_modem_fw(filename, filename, 1, argvmodem, progress_callback)) {
	    printf("error during 3G Modem flashing!\n");
    }

    ret = StringValue(strdup(""));
done:
    if (filename)
        free(filename);

    return ret;
}

void Register_libintel_updater(void)
{
    RegisterFunction("flash_osip", FlashOsipFn);
    RegisterFunction("flash_ifwi", FlashIfwiFn);
    RegisterFunction("flash_modem", FlashModemFn);
    RegisterFunction("extract_osip", ExtractOsipFn);
}