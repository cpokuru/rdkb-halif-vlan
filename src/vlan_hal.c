/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2024 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file vlan_hal.c
 * @brief Real implementation of the VLAN HAL APIs for the Filogic-GW (MediaTek RDK-B).
 *
 * Target environment:
 *   - brctl (bridge-utils 1.7) for bridge management
 *   - vconfig (BusyBox v1.35.0) for VLAN sub-interface creation
 *   - ip link for interface state management
 *   - 8021q built into kernel (no modprobe needed)
 *   - VLAN sub-interfaces named as <ifName>.<vlanID> (e.g. eth1.100)
 *   - Config persisted in /tmp/vlan_hal_config.db (volatile)
 */

/* Enable POSIX extensions for popen/pclose/rename */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "vlan_hal.h"

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

#define VLAN_CONFIG_FILE   "/tmp/vlan_hal_config.db"
#define VLAN_ID_MIN        1
#define VLAN_ID_MAX        4094
#define CMD_BUF_SIZE       256
#define LINE_BUF_SIZE      128

/*
 * sscanf width limits: VLAN_HAL_MAX_VLANGROUP_TEXT_LENGTH and
 * VLAN_HAL_MAX_VLANID_TEXT_LENGTH are both 32, so %31s leaves room for '\0'.
 */
#define SCAN_FMT_GROUP   "%31s"
#define SCAN_FMT_VLANID  "%31s"

#define VLAN_HAL_LOG(fmt, ...)  printf("[VLAN_HAL] " fmt "\n", ##__VA_ARGS__)

/* -------------------------------------------------------------------------
 * Helper: Shell output capture
 * ---------------------------------------------------------------------- */

/**
 * @brief Execute a shell command and read the first line of output into @p out.
 */
void _get_shell_outputbuffer(char *cmd, char *out, int len)
{
    FILE *fp;

    if (!cmd || !out || len <= 0)
        return;

    out[0] = '\0';
    fp = popen(cmd, "r");
    if (fp)
    {
        if (fgets(out, len, fp) == NULL)
            out[0] = '\0';
        pclose(fp);
    }
}

/**
 * @brief Read one line from an already-open FILE stream @p fp into @p out.
 */
void _get_shell_outputbuffer_res(FILE *fp, char *out, int len)
{
    if (fp && out && len > 0)
    {
        if (fgets(out, len, fp) == NULL)
            out[0] = '\0';
    }
}

/* -------------------------------------------------------------------------
 * Helper: Input sanitization
 * ---------------------------------------------------------------------- */

/**
 * @brief Return non-zero if every character of @p s is safe for use in a
 *        shell command as a network interface name or bridge name.
 *
 * Allowed: alphanumeric, hyphen (-), underscore (_).
 * Dots are intentionally excluded here because the sub-interface dot is
 * added by the implementation itself, not passed by the caller.
 */
static int is_safe_name(const char *s)
{
    if (!s || s[0] == '\0')
        return 0;
    while (*s)
    {
        char c = *s++;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return 0;
    }
    return 1;
}

/**
 * @brief Return non-zero if @p s is a non-empty string of decimal digits only.
 *
 * Used to validate VLAN IDs before embedding them in shell commands.
 */
static int is_safe_vlanid_str(const char *s)
{
    if (!s || s[0] == '\0')
        return 0;
    while (*s)
    {
        if (*s < '0' || *s > '9')
            return 0;
        s++;
    }
    return 1;
}

/* -------------------------------------------------------------------------
 * Helper: Bridge / interface existence checks
 * ---------------------------------------------------------------------- */

/**
 * @brief Check if a Linux bridge @p br_name exists via sysfs.
 *
 * @returns RETURN_OK if /sys/class/net/<br_name>/bridge/ is present,
 *          RETURN_ERR otherwise.
 */
int _is_this_group_available_in_linux_bridge(char *br_name)
{
    char path[CMD_BUF_SIZE];
    struct stat st;

    if (!br_name || br_name[0] == '\0')
        return RETURN_ERR;

    snprintf(path, sizeof(path), "/sys/class/net/%s/bridge", br_name);
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return RETURN_OK;

    return RETURN_ERR;
}

/**
 * @brief Check if VLAN sub-interface <if_name>.<vlanID> is a member of any bridge.
 *
 * Checks for the existence of /sys/class/net/<if_name>.<vlanID>/master.
 *
 * @returns RETURN_OK if found, RETURN_ERR otherwise.
 */
int _is_this_interface_available_in_linux_bridge(char *if_name, char *vlanID)
{
    char path[CMD_BUF_SIZE];
    struct stat st;

    if (!if_name || if_name[0] == '\0' || !vlanID || vlanID[0] == '\0')
        return RETURN_ERR;

    snprintf(path, sizeof(path), "/sys/class/net/%s.%s/master", if_name, vlanID);
    if (stat(path, &st) == 0)
        return RETURN_OK;

    return RETURN_ERR;
}

/**
 * @brief Check if VLAN sub-interface <if_name>.<vlanID> is in bridge @p br_name.
 *
 * Checks for /sys/class/net/<br_name>/brif/<if_name>.<vlanID>.
 *
 * @returns RETURN_OK if found, RETURN_ERR otherwise.
 */
int _is_this_interface_available_in_given_linux_bridge(char *if_name, char *br_name, char *vlanID)
{
    char path[CMD_BUF_SIZE];
    struct stat st;

    if (!if_name || if_name[0] == '\0' ||
        !br_name || br_name[0] == '\0' ||
        !vlanID  || vlanID[0]  == '\0')
        return RETURN_ERR;

    snprintf(path, sizeof(path), "/sys/class/net/%s/brif/%s.%s",
             br_name, if_name, vlanID);
    if (stat(path, &st) == 0)
        return RETURN_OK;

    return RETURN_ERR;
}

/* -------------------------------------------------------------------------
 * Config persistence helpers
 * ---------------------------------------------------------------------- */

/**
 * @brief Insert or update a VLAN config entry in VLAN_CONFIG_FILE.
 *
 * File format: one entry per line — "groupName vlanID\n"
 * If @p groupName already exists the line is updated in-place (via a temp file).
 */
int insert_VLAN_ConfigEntry(char *groupName, char *vlanID)
{
    FILE *fp, *ftmp;
    char  line[LINE_BUF_SIZE];
    char  tmpfile[] = "/tmp/vlan_hal_config.db.tmp";
    char  name[VLAN_HAL_MAX_VLANGROUP_TEXT_LENGTH];
    char  id[VLAN_HAL_MAX_VLANID_TEXT_LENGTH];
    int   found = 0;

    if (!groupName || groupName[0] == '\0' || !vlanID || vlanID[0] == '\0')
        return RETURN_ERR;

    fp = fopen(VLAN_CONFIG_FILE, "r");
    ftmp = fopen(tmpfile, "w");
    if (!ftmp)
    {
        if (fp) fclose(fp);
        VLAN_HAL_LOG("insert_VLAN_ConfigEntry: cannot open tmp file");
        return RETURN_ERR;
    }

    if (fp)
    {
        while (fgets(line, sizeof(line), fp))
        {
            if (sscanf(line, SCAN_FMT_GROUP " " SCAN_FMT_VLANID, name, id) == 2 &&
                strcmp(name, groupName) == 0)
            {
                /* Update existing entry */
                fprintf(ftmp, "%s %s\n", groupName, vlanID);
                found = 1;
            }
            else
            {
                fputs(line, ftmp);
            }
        }
        fclose(fp);
    }

    if (!found)
        fprintf(ftmp, "%s %s\n", groupName, vlanID);

    fclose(ftmp);
    rename(tmpfile, VLAN_CONFIG_FILE);
    return RETURN_OK;
}

/**
 * @brief Remove the config entry for @p groupName from VLAN_CONFIG_FILE.
 */
int delete_VLAN_ConfigEntry(char *groupName)
{
    FILE *fp, *ftmp;
    char  line[LINE_BUF_SIZE];
    char  tmpfile[] = "/tmp/vlan_hal_config.db.tmp";
    char  name[VLAN_HAL_MAX_VLANGROUP_TEXT_LENGTH];
    char  id[VLAN_HAL_MAX_VLANID_TEXT_LENGTH];

    if (!groupName || groupName[0] == '\0')
        return RETURN_ERR;

    fp = fopen(VLAN_CONFIG_FILE, "r");
    if (!fp)
        return RETURN_OK; /* Nothing to delete */

    ftmp = fopen(tmpfile, "w");
    if (!ftmp)
    {
        fclose(fp);
        VLAN_HAL_LOG("delete_VLAN_ConfigEntry: cannot open tmp file");
        return RETURN_ERR;
    }

    while (fgets(line, sizeof(line), fp))
    {
        if (sscanf(line, SCAN_FMT_GROUP " " SCAN_FMT_VLANID, name, id) == 2 &&
            strcmp(name, groupName) == 0)
            continue; /* Skip this entry */
        fputs(line, ftmp);
    }

    fclose(fp);
    fclose(ftmp);
    rename(tmpfile, VLAN_CONFIG_FILE);
    return RETURN_OK;
}

/**
 * @brief Look up the VLAN ID for @p groupName in VLAN_CONFIG_FILE.
 *
 * @param[out] vlanID  Buffer to receive the VLAN ID string.
 * @returns RETURN_OK if found, RETURN_ERR otherwise.
 */
int get_vlanId_for_GroupName(const char *groupName, char *vlanID)
{
    FILE *fp;
    char  line[LINE_BUF_SIZE];
    char  name[VLAN_HAL_MAX_VLANGROUP_TEXT_LENGTH];
    char  id[VLAN_HAL_MAX_VLANID_TEXT_LENGTH];

    if (!groupName || groupName[0] == '\0' || !vlanID)
        return RETURN_ERR;

    vlanID[0] = '\0';

    fp = fopen(VLAN_CONFIG_FILE, "r");
    if (!fp)
        return RETURN_ERR;

    while (fgets(line, sizeof(line), fp))
    {
        if (sscanf(line, SCAN_FMT_GROUP " " SCAN_FMT_VLANID, name, id) == 2 &&
            strcmp(name, groupName) == 0)
        {
            strncpy(vlanID, id, VLAN_HAL_MAX_VLANID_TEXT_LENGTH - 1);
            vlanID[VLAN_HAL_MAX_VLANID_TEXT_LENGTH - 1] = '\0';
            fclose(fp);
            return RETURN_OK;
        }
    }

    fclose(fp);
    return RETURN_ERR;
}

/**
 * @brief Print every line of VLAN_CONFIG_FILE to stdout.
 */
int print_all_vlanId_Configuration(void)
{
    FILE *fp;
    char  line[LINE_BUF_SIZE];

    fp = fopen(VLAN_CONFIG_FILE, "r");
    if (!fp)
    {
        VLAN_HAL_LOG("print_all_vlanId_Configuration: config file not found");
        return RETURN_OK; /* Empty config is not an error */
    }

    while (fgets(line, sizeof(line), fp))
        printf("%s", line);

    fclose(fp);
    return RETURN_OK;
}

/* -------------------------------------------------------------------------
 * Public APIs
 * ---------------------------------------------------------------------- */

/**
 * @brief Creates a new VLAN bridge group with the given default VLAN ID.
 *
 * Steps:
 *   1. Validate inputs.
 *   2. If bridge already exists, verify stored vlanID matches; return
 *      RETURN_OK on match, RETURN_ERR on mismatch.
 *   3. Create bridge via "brctl addbr <groupName>".
 *   4. Bring the bridge up via "ip link set <groupName> up".
 *   5. Persist config entry.
 */
int vlan_hal_addGroup(const char *groupName, const char *default_vlanID)
{
    char cmd[CMD_BUF_SIZE];
    char out[CMD_BUF_SIZE];
    char stored_vlanID[VLAN_HAL_MAX_VLANID_TEXT_LENGTH];
    int  vid;

    if (!groupName || groupName[0] == '\0' ||
        !default_vlanID || default_vlanID[0] == '\0')
    {
        VLAN_HAL_LOG("vlan_hal_addGroup: invalid arguments");
        return RETURN_ERR;
    }

    if (!is_safe_name(groupName) || !is_safe_vlanid_str(default_vlanID))
    {
        VLAN_HAL_LOG("vlan_hal_addGroup: unsafe characters in arguments");
        return RETURN_ERR;
    }

    vid = atoi(default_vlanID);
    if (vid < VLAN_ID_MIN || vid > VLAN_ID_MAX)
    {
        VLAN_HAL_LOG("vlan_hal_addGroup: vlanID %d out of range [%d-%d]",
                     vid, VLAN_ID_MIN, VLAN_ID_MAX);
        return RETURN_ERR;
    }

    /* Check if bridge already exists */
    if (_is_this_group_available_in_linux_bridge((char *)groupName) == RETURN_OK)
    {
        if (get_vlanId_for_GroupName(groupName, stored_vlanID) == RETURN_OK &&
            strcmp(stored_vlanID, default_vlanID) == 0)
        {
            VLAN_HAL_LOG("vlan_hal_addGroup: group %s already exists with vlanID %s",
                         groupName, default_vlanID);
            return RETURN_OK;
        }
        VLAN_HAL_LOG("vlan_hal_addGroup: group %s exists with different vlanID", groupName);
        return RETURN_ERR;
    }

    /* Create the bridge */
    snprintf(cmd, sizeof(cmd), "brctl addbr %s 2>&1", groupName);
    _get_shell_outputbuffer(cmd, out, sizeof(out));
    if (out[0] != '\0')
    {
        VLAN_HAL_LOG("vlan_hal_addGroup: brctl addbr error: %s", out);
        return RETURN_ERR;
    }

    /* Bring it up */
    snprintf(cmd, sizeof(cmd), "ip link set %s up 2>&1", groupName);
    _get_shell_outputbuffer(cmd, out, sizeof(out));
    if (out[0] != '\0')
        VLAN_HAL_LOG("vlan_hal_addGroup: ip link set up warning: %s", out);

    /* Persist */
    insert_VLAN_ConfigEntry((char *)groupName, (char *)default_vlanID);

    VLAN_HAL_LOG("vlan_hal_addGroup: created bridge %s vlanID %s", groupName, default_vlanID);
    return RETURN_OK;
}

/**
 * @brief Deletes an existing VLAN bridge group and its interfaces.
 *
 * Steps:
 *   1. Validate input.
 *   2. If bridge doesn't exist, return RETURN_OK (idempotent).
 *   3. Remove all member interfaces.
 *   4. Bring bridge down.
 *   5. Delete bridge.
 *   6. Remove config entry.
 */
int vlan_hal_delGroup(const char *groupName)
{
    char cmd[CMD_BUF_SIZE];
    char out[CMD_BUF_SIZE];

    if (!groupName || groupName[0] == '\0')
    {
        VLAN_HAL_LOG("vlan_hal_delGroup: invalid argument");
        return RETURN_ERR;
    }

    if (!is_safe_name(groupName))
    {
        VLAN_HAL_LOG("vlan_hal_delGroup: unsafe characters in groupName");
        return RETURN_ERR;
    }

    if (_is_this_group_available_in_linux_bridge((char *)groupName) != RETURN_OK)
    {
        VLAN_HAL_LOG("vlan_hal_delGroup: group %s does not exist, nothing to do", groupName);
        return RETURN_OK;
    }

    /* Remove all interfaces first */
    vlan_hal_delete_all_Interfaces(groupName);

    /* Bring bridge down */
    snprintf(cmd, sizeof(cmd), "ip link set %s down 2>&1", groupName);
    _get_shell_outputbuffer(cmd, out, sizeof(out));
    if (out[0] != '\0')
        VLAN_HAL_LOG("vlan_hal_delGroup: ip link set down warning: %s", out);

    /* Delete bridge */
    snprintf(cmd, sizeof(cmd), "brctl delbr %s 2>&1", groupName);
    _get_shell_outputbuffer(cmd, out, sizeof(out));
    if (out[0] != '\0')
    {
        VLAN_HAL_LOG("vlan_hal_delGroup: brctl delbr error: %s", out);
        return RETURN_ERR;
    }

    delete_VLAN_ConfigEntry((char *)groupName);

    VLAN_HAL_LOG("vlan_hal_delGroup: deleted bridge %s", groupName);
    return RETURN_OK;
}

/**
 * @brief Adds interface <ifName>.<vlanID> to bridge @p groupName.
 *
 * Steps:
 *   1. Validate inputs and vlanID range.
 *   2. Check bridge exists.
 *   3. If sub-interface already in bridge, return RETURN_OK.
 *   4. If sub-interface doesn't exist in sysfs, create it with vconfig.
 *   5. Add sub-interface to bridge with brctl addif.
 */
int vlan_hal_addInterface(const char *groupName, const char *ifName, const char *vlanID)
{
    char cmd[CMD_BUF_SIZE];
    char out[CMD_BUF_SIZE];
    char sysfs_path[CMD_BUF_SIZE];
    struct stat st;
    int  vid;

    if (!groupName || groupName[0] == '\0' ||
        !ifName    || ifName[0]    == '\0' ||
        !vlanID    || vlanID[0]    == '\0')
    {
        VLAN_HAL_LOG("vlan_hal_addInterface: invalid arguments");
        return RETURN_ERR;
    }

    if (!is_safe_name(groupName) || !is_safe_name(ifName) || !is_safe_vlanid_str(vlanID))
    {
        VLAN_HAL_LOG("vlan_hal_addInterface: unsafe characters in arguments");
        return RETURN_ERR;
    }

    vid = atoi(vlanID);
    if (vid < VLAN_ID_MIN || vid > VLAN_ID_MAX)
    {
        VLAN_HAL_LOG("vlan_hal_addInterface: vlanID %d out of range", vid);
        return RETURN_ERR;
    }

    if (_is_this_group_available_in_linux_bridge((char *)groupName) != RETURN_OK)
    {
        VLAN_HAL_LOG("vlan_hal_addInterface: bridge %s does not exist", groupName);
        return RETURN_ERR;
    }

    /* Already a member of this bridge? */
    if (_is_this_interface_available_in_given_linux_bridge(
            (char *)ifName, (char *)groupName, (char *)vlanID) == RETURN_OK)
    {
        VLAN_HAL_LOG("vlan_hal_addInterface: %s.%s already in bridge %s",
                     ifName, vlanID, groupName);
        return RETURN_OK;
    }

    /* Create VLAN sub-interface if it doesn't exist yet */
    snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/net/%s.%s", ifName, vlanID);
    if (stat(sysfs_path, &st) != 0)
    {
        snprintf(cmd, sizeof(cmd), "vconfig add %s %s 2>&1", ifName, vlanID);
        _get_shell_outputbuffer(cmd, out, sizeof(out));
        if (out[0] != '\0')
            VLAN_HAL_LOG("vlan_hal_addInterface: vconfig add warning: %s", out);

        snprintf(cmd, sizeof(cmd), "ip link set %s.%s up 2>&1", ifName, vlanID);
        _get_shell_outputbuffer(cmd, out, sizeof(out));
        if (out[0] != '\0')
            VLAN_HAL_LOG("vlan_hal_addInterface: ip link set up warning: %s", out);
    }

    /* Add sub-interface to bridge */
    snprintf(cmd, sizeof(cmd), "brctl addif %s %s.%s 2>&1", groupName, ifName, vlanID);
    _get_shell_outputbuffer(cmd, out, sizeof(out));
    if (out[0] != '\0')
    {
        VLAN_HAL_LOG("vlan_hal_addInterface: brctl addif error: %s", out);
        return RETURN_ERR;
    }

    VLAN_HAL_LOG("vlan_hal_addInterface: added %s.%s to bridge %s", ifName, vlanID, groupName);
    return RETURN_OK;
}

/**
 * @brief Removes interface <ifName>.<vlanID> from bridge @p groupName.
 *
 * Steps:
 *   1. Validate inputs and vlanID range.
 *   2. If sub-interface is not in the bridge, return RETURN_OK (idempotent).
 *   3. Remove from bridge with brctl delif.
 *   4. Bring sub-interface down.
 *   5. Remove sub-interface with vconfig rem.
 */
int vlan_hal_delInterface(const char *groupName, const char *ifName, const char *vlanID)
{
    char cmd[CMD_BUF_SIZE];
    char out[CMD_BUF_SIZE];
    int  vid;

    if (!groupName || groupName[0] == '\0' ||
        !ifName    || ifName[0]    == '\0' ||
        !vlanID    || vlanID[0]    == '\0')
    {
        VLAN_HAL_LOG("vlan_hal_delInterface: invalid arguments");
        return RETURN_ERR;
    }

    if (!is_safe_name(groupName) || !is_safe_name(ifName) || !is_safe_vlanid_str(vlanID))
    {
        VLAN_HAL_LOG("vlan_hal_delInterface: unsafe characters in arguments");
        return RETURN_ERR;
    }

    vid = atoi(vlanID);
    if (vid < VLAN_ID_MIN || vid > VLAN_ID_MAX)
    {
        VLAN_HAL_LOG("vlan_hal_delInterface: vlanID %d out of range", vid);
        return RETURN_ERR;
    }

    /* Not in bridge — nothing to do */
    if (_is_this_interface_available_in_given_linux_bridge(
            (char *)ifName, (char *)groupName, (char *)vlanID) != RETURN_OK)
    {
        VLAN_HAL_LOG("vlan_hal_delInterface: %s.%s not in bridge %s, nothing to do",
                     ifName, vlanID, groupName);
        return RETURN_OK;
    }

    /* Remove from bridge */
    snprintf(cmd, sizeof(cmd), "brctl delif %s %s.%s 2>&1", groupName, ifName, vlanID);
    _get_shell_outputbuffer(cmd, out, sizeof(out));
    if (out[0] != '\0')
        VLAN_HAL_LOG("vlan_hal_delInterface: brctl delif warning: %s", out);

    /* Bring sub-interface down */
    snprintf(cmd, sizeof(cmd), "ip link set %s.%s down 2>&1", ifName, vlanID);
    _get_shell_outputbuffer(cmd, out, sizeof(out));
    if (out[0] != '\0')
        VLAN_HAL_LOG("vlan_hal_delInterface: ip link set down warning: %s", out);

    /* Remove VLAN sub-interface */
    snprintf(cmd, sizeof(cmd), "vconfig rem %s.%s 2>&1", ifName, vlanID);
    _get_shell_outputbuffer(cmd, out, sizeof(out));
    if (out[0] != '\0')
        VLAN_HAL_LOG("vlan_hal_delInterface: vconfig rem warning: %s", out);

    VLAN_HAL_LOG("vlan_hal_delInterface: removed %s.%s from bridge %s",
                 ifName, vlanID, groupName);
    return RETURN_OK;
}

/**
 * @brief Removes all interfaces currently in bridge @p groupName.
 *
 * Parses "brctl show <groupName>" output to enumerate member interfaces,
 * then calls vlan_hal_delInterface() for VLAN sub-interfaces (containing a '.')
 * or issues a direct "brctl delif" for non-VLAN members.
 */
int vlan_hal_delete_all_Interfaces(const char *groupName)
{
    char  cmd[CMD_BUF_SIZE];
    char  line[LINE_BUF_SIZE];
    char  out[CMD_BUF_SIZE];
    FILE *fp;
    int   line_num = 0;

    if (!groupName || groupName[0] == '\0')
    {
        VLAN_HAL_LOG("vlan_hal_delete_all_Interfaces: invalid argument");
        return RETURN_ERR;
    }

    snprintf(cmd, sizeof(cmd), "brctl show %s 2>/dev/null", groupName);
    fp = popen(cmd, "r");
    if (!fp)
    {
        VLAN_HAL_LOG("vlan_hal_delete_all_Interfaces: popen failed");
        return RETURN_ERR;
    }

    while (fgets(line, sizeof(line), fp))
    {
        char iface[VLAN_HAL_MAX_INTERFACE_NAME_TEXT_LENGTH];
        char *dot;

        /* Strip newline */
        line[strcspn(line, "\r\n")] = '\0';

        line_num++;
        if (line_num == 1)
            continue; /* Header line: "bridge name  bridge id  STP enabled  interfaces" */

        /*
         * brctl show output columns:
         *   line 2: "<brName>  <bridgeId>  <stp>  [ifName]"
         *   line 3+: "                              [ifName]"
         * Extract the last whitespace-separated token on each line.
         */
        {
            char *token = NULL;
            char *last  = NULL;
            char  tmp[LINE_BUF_SIZE];

            strncpy(tmp, line, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';

            token = strtok(tmp, " \t");
            while (token)
            {
                last  = token;
                token = strtok(NULL, " \t");
            }

            if (!last || last[0] == '\0')
                continue;

            strncpy(iface, last, sizeof(iface) - 1);
            iface[sizeof(iface) - 1] = '\0';
        }

        /* Skip if what we got looks like a bridge ID (xx:xx:xx:xx:xx:xx) */
        if (strchr(iface, ':') != NULL)
            continue;
        /* Skip "yes"/"no" STP tokens */
        if (strcmp(iface, "yes") == 0 || strcmp(iface, "no") == 0)
            continue;
        /* Skip the bridge name itself */
        if (strcmp(iface, groupName) == 0)
            continue;

        dot = strchr(iface, '.');
        if (dot)
        {
            /* VLAN sub-interface: split into parent and vlanID */
            char parent[VLAN_HAL_MAX_INTERFACE_NAME_TEXT_LENGTH];
            char vid_str[VLAN_HAL_MAX_VLANID_TEXT_LENGTH];
            size_t plen = (size_t)(dot - iface);

            if (plen >= sizeof(parent))
                plen = sizeof(parent) - 1;
            strncpy(parent, iface, plen);
            parent[plen] = '\0';
            strncpy(vid_str, dot + 1, sizeof(vid_str) - 1);
            vid_str[sizeof(vid_str) - 1] = '\0';

            /* Validate parsed names before using in shell commands */
            if (!is_safe_name(parent) || !is_safe_vlanid_str(vid_str))
            {
                VLAN_HAL_LOG("vlan_hal_delete_all_Interfaces: unsafe iface skipped: %s", iface);
                continue;
            }

            vlan_hal_delInterface(groupName, parent, vid_str);
        }
        else
        {
            /* Non-VLAN bridge member: validate then remove from bridge */
            if (!is_safe_name(iface))
            {
                VLAN_HAL_LOG("vlan_hal_delete_all_Interfaces: unsafe iface skipped: %s", iface);
                continue;
            }
            snprintf(cmd, sizeof(cmd), "brctl delif %s %s 2>&1", groupName, iface);
            _get_shell_outputbuffer(cmd, out, sizeof(out));
            if (out[0] != '\0')
                VLAN_HAL_LOG("vlan_hal_delete_all_Interfaces: brctl delif warning: %s", out);
        }
    }

    pclose(fp);
    VLAN_HAL_LOG("vlan_hal_delete_all_Interfaces: removed all interfaces from %s", groupName);
    return RETURN_OK;
}

/**
 * @brief Print the bridge config for @p groupName and its stored VLAN ID.
 */
int vlan_hal_printGroup(const char *groupName)
{
    char cmd[CMD_BUF_SIZE];
    char out[CMD_BUF_SIZE];
    char vlanID[VLAN_HAL_MAX_VLANID_TEXT_LENGTH];
    FILE *fp;

    if (!groupName || groupName[0] == '\0')
    {
        VLAN_HAL_LOG("vlan_hal_printGroup: invalid argument");
        return RETURN_ERR;
    }

    if (!is_safe_name(groupName))
    {
        VLAN_HAL_LOG("vlan_hal_printGroup: unsafe characters in groupName");
        return RETURN_ERR;
    }

    snprintf(cmd, sizeof(cmd), "brctl show %s 2>/dev/null", groupName);
    fp = popen(cmd, "r");
    if (fp)
    {
        while (fgets(out, sizeof(out), fp))
            printf("%s", out);
        pclose(fp);
    }

    if (get_vlanId_for_GroupName(groupName, vlanID) == RETURN_OK)
        printf("Group: %s  VLAN ID: %s\n", groupName, vlanID);
    else
        printf("Group: %s  VLAN ID: (not configured)\n", groupName);

    return RETURN_OK;
}

/**
 * @brief Print the bridge config for all groups and all stored VLAN IDs.
 */
int vlan_hal_printAllGroup(void)
{
    char  out[CMD_BUF_SIZE];
    FILE *fp;

    fp = popen("brctl show 2>/dev/null", "r");
    if (fp)
    {
        while (fgets(out, sizeof(out), fp))
            printf("%s", out);
        pclose(fp);
    }

    print_all_vlanId_Configuration();
    return RETURN_OK;
}
