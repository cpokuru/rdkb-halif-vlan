#include <stdio.h>
#include "vlan_hal.h"

int main(void)
{
    int ret;
    char vlanID[32] = {0};

    printf("=== VLAN HAL Test ===\n\n");

    /* 1. Add a VLAN group */
    printf("-- Test: vlan_hal_addGroup --\n");
    ret = vlan_hal_addGroup("brtest0", "200");
    printf("Result: %s\n\n", ret == 0 ? "PASS (RETURN_OK)" : "FAIL (RETURN_ERR)");

    /* 2. Print the group */
    printf("-- Test: vlan_hal_printGroup --\n");
    ret = vlan_hal_printGroup("brtest0");
    printf("Result: %s\n\n", ret == 0 ? "PASS" : "FAIL");

    /* 3. Add interface to group */
    printf("-- Test: vlan_hal_addInterface --\n");
    ret = vlan_hal_addInterface("brtest0", "eth2", "200");
    printf("Result: %s\n\n", ret == 0 ? "PASS" : "FAIL");

    /* 4. Print all groups */
    printf("-- Test: vlan_hal_printAllGroup --\n");
    ret = vlan_hal_printAllGroup();
    printf("Result: %s\n\n", ret == 0 ? "PASS" : "FAIL");

    /* 5. Get VLAN ID for group */
    printf("-- Test: get_vlanId_for_GroupName --\n");
    ret = get_vlanId_for_GroupName("brtest0", vlanID);
    printf("vlanID retrieved: '%s'\nResult: %s\n\n", vlanID, ret == 0 ? "PASS" : "FAIL");

    /* 6. Remove interface */
    printf("-- Test: vlan_hal_delInterface --\n");
    ret = vlan_hal_delInterface("brtest0", "eth2", "200");
    printf("Result: %s\n\n", ret == 0 ? "PASS" : "FAIL");

    /* 7. Delete all interfaces */
    printf("-- Test: vlan_hal_delete_all_Interfaces --\n");
    ret = vlan_hal_delete_all_Interfaces("brtest0");
    printf("Result: %s\n\n", ret == 0 ? "PASS" : "FAIL");

    /* 8. Delete the group */
    printf("-- Test: vlan_hal_delGroup --\n");
    ret = vlan_hal_delGroup("brtest0");
    printf("Result: %s\n\n", ret == 0 ? "PASS" : "FAIL");

    printf("=== Test Complete ===\n");
    return 0;
}
