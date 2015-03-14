/*++

Module Name:

    driver.h

Abstract:

    This file contains the driver definitions.

Environment:

    Kernel-mode Driver Framework

--*/

#define INITGUID

#include <ntddk.h>
#include <wdf.h>
//#include <wdmsec.h> // for SDDLs

#include "device.h"
#include "queue.h"
#include "trace.h"

typedef struct _CONTROL_DEVICE_EXTENSION {

    HANDLE   FileHandle; // Store your control data here

} CONTROL_DEVICE_EXTENSION, *PCONTROL_DEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CONTROL_DEVICE_EXTENSION,
    ControlGetData)


//
// WDFDRIVER Events
//

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD cadwdEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP cadwdEvtDriverContextCleanup;

EVT_WDF_DRIVER_UNLOAD cadwdEvtDriverUnload;

VOID
cadwdShutdown(
WDFDEVICE Device
);
