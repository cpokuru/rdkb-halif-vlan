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
 * @brief Stub/dummy implementation of the VLAN HAL APIs declared in vlan_hal.h.
 *
 * This file provides a no-op stub implementation suitable for build validation,
 * unit-test scaffolding, and platform bring-up. No real system calls or bridge
 * commands are performed; every function logs its invocation and returns
 * RETURN_OK (or is a no-op for void functions).
 */

#include <stdio.h>
#include "vlan_hal.h"

/* -------------------------------------------------------------------------
 * Public APIs
 * ---------------------------------------------------------------------- */

/**
 * @brief Stub: Creates a new VLAN group with the given default VLAN ID.
 */
int vlan_hal_addGroup(const char *groupName, const char *default_vlanID)
{
    printf("[VLAN_HAL STUB] vlan_hal_addGroup called: groupName=%s, vlanID=%s\n",
           groupName, default_vlanID);
    return RETURN_OK;
}

/**
 * @brief Stub: Deletes an existing VLAN group.
 */
int vlan_hal_delGroup(const char *groupName)
{
    printf("[VLAN_HAL STUB] vlan_hal_delGroup called: groupName=%s\n", groupName);
    return RETURN_OK;
}

/**
 * @brief Stub: Adds an interface to a VLAN group.
 */
int vlan_hal_addInterface(const char *groupName, const char *ifName, const char *vlanID)
{
    printf("[VLAN_HAL STUB] vlan_hal_addInterface called: groupName=%s, ifName=%s, vlanID=%s\n",
           groupName, ifName, vlanID);
    return RETURN_OK;
}

/**
 * @brief Stub: Removes an interface from a VLAN group.
 */
int vlan_hal_delInterface(const char *groupName, const char *ifName, const char *vlanID)
{
    printf("[VLAN_HAL STUB] vlan_hal_delInterface called: groupName=%s, ifName=%s, vlanID=%s\n",
           groupName, ifName, vlanID);
    return RETURN_OK;
}

/**
 * @brief Stub: Prints the settings of a specific VLAN group.
 */
int vlan_hal_printGroup(const char *groupName)
{
    printf("[VLAN_HAL STUB] vlan_hal_printGroup called: groupName=%s\n", groupName);
    return RETURN_OK;
}

/**
 * @brief Stub: Prints the settings of all VLAN groups.
 */
int vlan_hal_printAllGroup()
{
    printf("[VLAN_HAL STUB] vlan_hal_printAllGroup called\n");
    return RETURN_OK;
}

/**
 * @brief Stub: Removes all interfaces from a VLAN group.
 */
int vlan_hal_delete_all_Interfaces(const char *groupName)
{
    printf("[VLAN_HAL STUB] vlan_hal_delete_all_Interfaces called: groupName=%s\n", groupName);
    return RETURN_OK;
}

/* -------------------------------------------------------------------------
 * Utility APIs
 * ---------------------------------------------------------------------- */

/**
 * @brief Stub: Checks if a bridge is present in the Linux bridge system.
 */
int _is_this_group_available_in_linux_bridge(char *br_name)
{
    printf("[VLAN_HAL STUB] _is_this_group_available_in_linux_bridge called: br_name=%s\n",
           br_name);
    return RETURN_OK;
}

/**
 * @brief Stub: Checks if an interface exists in any Linux bridge.
 */
int _is_this_interface_available_in_linux_bridge(char *if_name, char *vlanID)
{
    printf("[VLAN_HAL STUB] _is_this_interface_available_in_linux_bridge called: if_name=%s, vlanID=%s\n",
           if_name, vlanID);
    return RETURN_OK;
}

/**
 * @brief Stub: Checks if an interface is a member of a specific Linux bridge.
 */
int _is_this_interface_available_in_given_linux_bridge(char *if_name, char *br_name, char *vlanID)
{
    printf("[VLAN_HAL STUB] _is_this_interface_available_in_given_linux_bridge called: if_name=%s, br_name=%s, vlanID=%s\n",
           if_name, br_name, vlanID);
    return RETURN_OK;
}

/**
 * @brief Stub: Retrieves output from a shell command into a buffer.
 */
void _get_shell_outputbuffer(char *cmd, char *out, int len)
{
    printf("[VLAN_HAL STUB] _get_shell_outputbuffer called: cmd=%s, len=%d\n", cmd, len);
    (void)out;
}

/**
 * @brief Stub: Retrieves output from a FILE stream into a buffer.
 */
void _get_shell_outputbuffer_res(FILE *fp, char *out, int len)
{
    printf("[VLAN_HAL STUB] _get_shell_outputbuffer_res called: len=%d\n", len);
    (void)fp;
    (void)out;
}

/**
 * @brief Stub: Inserts a VLAN configuration entry for a group.
 */
int insert_VLAN_ConfigEntry(char *groupName, char *vlanID)
{
    printf("[VLAN_HAL STUB] insert_VLAN_ConfigEntry called: groupName=%s, vlanID=%s\n",
           groupName, vlanID);
    return RETURN_OK;
}

/**
 * @brief Stub: Deletes the VLAN configuration entry for a group.
 */
int delete_VLAN_ConfigEntry(char *groupName)
{
    printf("[VLAN_HAL STUB] delete_VLAN_ConfigEntry called: groupName=%s\n", groupName);
    return RETURN_OK;
}

/**
 * @brief Stub: Retrieves the VLAN ID associated with a given group name.
 *
 * Zero-initialises the output vlanID buffer and returns RETURN_OK.
 */
int get_vlanId_for_GroupName(const char *groupName, char *vlanID)
{
    printf("[VLAN_HAL STUB] get_vlanId_for_GroupName called: groupName=%s\n", groupName);
    if (vlanID != NULL)
    {
        vlanID[0] = '\0';
    }
    return RETURN_OK;
}

/**
 * @brief Stub: Prints all stored VLAN ID and group name configurations.
 */
int print_all_vlanId_Configuration(void)
{
    printf("[VLAN_HAL STUB] print_all_vlanId_Configuration called\n");
    return RETURN_OK;
}
