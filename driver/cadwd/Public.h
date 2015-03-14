/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define an Interface Guid so that app can find the device and talk to it.
//

DEFINE_GUID (GUID_DEVINTERFACE_cadwd,
    0xb7fe2f76,0x2b97,0x4c14,0xaf,0x83,0x25,0xae,0x6e,0x1b,0x09,0x01);
// {b7fe2f76-2b97-4c14-af83-25ae6e1b0901}
