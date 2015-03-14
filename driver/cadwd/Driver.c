/*++

Module Name:

    driver.c

Abstract:

    This file contains the driver entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "driver.tmh"

#include <ntdef.h>

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, cadwdEvtDeviceAdd)
#pragma alloc_text (PAGE, cadwdEvtDriverContextCleanup)
#pragma alloc_text (PAGE, cadwdEvtDriverUnload)
#endif


NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:
    DriverEntry initializes the driver and is the first routine called by the
    system after the driver is loaded. DriverEntry specifies the other entry
    points in the function driver, such as EvtDevice and DriverUnload.

Parameters Description:

    DriverObject - represents the instance of the function driver that is loaded
    into memory. DriverEntry must initialize members of DriverObject before it
    returns to the caller. DriverObject is allocated by the system before the
    driver is loaded, and it is released by the system after the system unloads
    the function driver from memory.

    RegistryPath - represents the driver specific path in the Registry.
    The function driver can use the path to store driver related data between
    reboots. The path does not store hardware instance specific data.

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise.

--*/
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDRIVER                      hDriver;
    PWDFDEVICE_INIT                pInit = NULL;

    //KdBreakPoint();

    //
    // Initialize WPP Tracing
    //
    WPP_INIT_TRACING( DriverObject, RegistryPath );

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = cadwdEvtDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config,
                           WDF_NO_EVENT_CALLBACK/*cadwdEvtDeviceAdd*/ // This is a non-pnp driver.
                           );

    //
    // Tell the framework that this is non-pnp driver so that it doesn't
    // set the default AddDevice routine.
    //
    config.DriverInitFlags |= WdfDriverInitNonPnpDriver;

    //
    // NonPnp driver must explicitly register an unload routine for
    // the driver to be unloaded.
    //
    config.EvtDriverUnload = cadwdEvtDriverUnload;

    UNICODE_STRING ntDeviceName, symbolicLinkName;
    RtlInitUnicodeString(&ntDeviceName, L"\\Device\\cadwd");
    RtlInitUnicodeString(&symbolicLinkName, L"\\DosDevices\\cadwd");
    //DECLARE_CONST_UNICODE_STRING(ntDeviceName, L"\\Device\\cadwd");
    //DECLARE_CONST_UNICODE_STRING(symbolicLinkName, L"\\DosDevices\\cadwd");
    KdPrint(("===============================================================================\n"));
    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             &attributes,
                             &config,
                             &hDriver/*WDF_NO_HANDLE*/
                             );

    //IoCreateSymbolicLink(&symbolicLinkName, &ntDeviceName);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfDriverCreate failed %!STATUS!", status);
        WPP_CLEANUP(DriverObject);
        return status;
    }

    //
    //
    // In order to create a control device, we first need to allocate a
    // WDFDEVICE_INIT structure and set all properties.
    //
    pInit = WdfControlDeviceInitAllocate(
        hDriver,
        &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R
        );

    if (pInit == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        return status;
    }

    //
    // Call cadwdEvtDeviceAdd to create a deviceobject to represent our
    // software device.
    //
    status = cadwdEvtDeviceAdd(hDriver, pInit);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}

NTSTATUS
cadwdEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
/*++
Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. We create and initialize a device object to
    represent a new instance of the device.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

    NTSTATUS

--*/
{
    /*
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    status = cadwdCreateDevice(DeviceInit);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
    */

    NTSTATUS                       status;
    WDF_OBJECT_ATTRIBUTES           attributes;
    WDF_IO_QUEUE_CONFIG      ioQueueConfig;
    WDF_FILEOBJECT_CONFIG fileConfig;
    WDFQUEUE                            queue;
    WDFDEVICE   controlDevice;
    DECLARE_CONST_UNICODE_STRING(ntDeviceName, L"\\Device\\cadwd");
    DECLARE_CONST_UNICODE_STRING(symbolicLinkName, L"\\DosDevices\\cadwd");

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // Set exclusive to TRUE so that no more than one app can talk to the
    // control device at any time.
    //
    WdfDeviceInitSetExclusive(DeviceInit, TRUE);

    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoBuffered);


    status = WdfDeviceInitAssignName(DeviceInit, &ntDeviceName);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfDeviceInitAssignName failed %!STATUS!", status);
        goto End;
    }

    WdfControlDeviceInitSetShutdownNotification(DeviceInit,
        cadwdShutdown,
        WdfDeviceShutdown);

    //
    // Initialize WDF_FILEOBJECT_CONFIG_INIT struct to tell the
    // framework whether you are interested in handling Create, Close and
    // Cleanup requests that gets generated when an application or another
    // kernel component opens an handle to the device. If you don't register
    // the framework default behaviour would be to complete these requests
    // with STATUS_SUCCESS. A driver might be interested in registering these
    // events if it wants to do security validation and also wants to maintain
    // per handle (fileobject) context.
    //

    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        NULL/*NonPnpEvtDeviceFileCreate*/,
        NULL/*NonPnpEvtFileClose*/,
        WDF_NO_EVENT_CALLBACK // not interested in Cleanup
        );

    WdfDeviceInitSetFileObjectConfig(DeviceInit,
        &fileConfig,
        WDF_NO_OBJECT_ATTRIBUTES);

    //
    // In order to support METHOD_NEITHER Device controls, or
    // NEITHER device I/O type, we need to register for the
    // EvtDeviceIoInProcessContext callback so that we can handle the request
    // in the calling threads context.
    //
    ///WdfDeviceInitSetIoInCallerContextCallback(DeviceInit,
    ///    NonPnpEvtDeviceIoInCallerContext);

    //
    // Specify the size of device context
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,
                                            CONTROL_DEVICE_EXTENSION);

    status = WdfDeviceCreate(&DeviceInit,
        &attributes,
        &controlDevice);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfDeviceCreate failed %!STATUS!", status);
        goto End;
    }

    //
    // Create a symbolic link for the control object so that usermode can open
    // the device.
    //


    status = WdfDeviceCreateSymbolicLink(controlDevice,
        &symbolicLinkName);

    if (!NT_SUCCESS(status)) {
        //
        // Control device will be deleted automatically by the framework.
        //
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfDeviceCreateSymbolicLink failed %!STATUS!", status);
        goto End;
    }

    //
    // Configure a default queue so that requests that are not
    // configure-fowarded using WdfDeviceConfigureRequestDispatching to goto
    // other queues get dispatched here.
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig,
        WdfIoQueueDispatchSequential);

    ///ioQueueConfig.EvtIoRead = FileEvtIoRead;
    ///ioQueueConfig.EvtIoWrite = FileEvtIoWrite;
    ///ioQueueConfig.EvtIoDeviceControl = FileEvtIoDeviceControl;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    //
    // Since we are using Zw function set execution level to passive so that
    // framework ensures that our Io callbacks called at only passive-level
    // even if the request came in at DISPATCH_LEVEL from another driver.
    //
    //attributes.ExecutionLevel = WdfExecutionLevelPassive;

    //
    // By default, Static Driver Verifier (SDV) displays a warning if it 
    // doesn't find the EvtIoStop callback on a power-managed queue. 
    // The 'assume' below causes SDV to suppress this warning. If the driver 
    // has not explicitly set PowerManaged to WdfFalse, the framework creates
    // power-managed queues when the device is not a filter driver.  Normally 
    // the EvtIoStop is required for power-managed queues, but for this driver
    // it is not needed b/c the driver doesn't hold on to the requests or 
    // forward them to other drivers. This driver completes the requests 
    // directly in the queue's handlers. If the EvtIoStop callback is not 
    // implemented, the framework waits for all driver-owned requests to be
    // done before moving in the Dx/sleep states or before removing the 
    // device, which is the correct behavior for this type of driver.
    // If the requests were taking an indeterminate amount of time to complete,
    // or if the driver forwarded the requests to a lower driver/another stack,
    // the queue should have an EvtIoStop/EvtIoResume.
    //
    __analysis_assume(ioQueueConfig.EvtIoStop != 0);
    status = WdfIoQueueCreate(controlDevice,
        &ioQueueConfig,
        &attributes,
        &queue // pointer to default queue
        );
    __analysis_assume(ioQueueConfig.EvtIoStop == 0);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfIoQueueCreate failed %!STATUS!", status);
        goto End;
    }

    //
    // Control devices must notify WDF when they are done initializing.   I/O is
    // rejected until this call is made.
    //
    WdfControlFinishInitializing(controlDevice);

End:
    //
    // If the device is created successfully, framework would clear the
    // DeviceInit value. Otherwise device create must have failed so we
    // should free the memory ourself.
    //
    if (DeviceInit != NULL) {
        WdfDeviceInitFree(DeviceInit);
    }

    return status;
}

VOID
cadwdEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
/*++
Routine Description:

    Free all the resources allocated in DriverEntry.

Arguments:

    DriverObject - handle to a WDF Driver object.

Return Value:

    VOID.

--*/
{
    UNREFERENCED_PARAMETER(DriverObject);

    PAGED_CODE ();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // Stop WPP Tracing
    //
    WPP_CLEANUP( WdfDriverWdmGetDriverObject(DriverObject) );

}


VOID
cadwdEvtDriverUnload(
IN WDFDRIVER Driver
)
/*++
Routine Description:

Called by the I/O subsystem just before unloading the driver.
You can free the resources created in the DriverEntry either
in this routine or in the EvtDriverContextCleanup callback.

Arguments:

Driver - Handle to a framework driver object created in DriverEntry

Return Value:

NTSTATUS

--*/
{
    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    return;
}


VOID
cadwdShutdown(
WDFDEVICE Device
)
/*++

Routine Description:
Callback invoked when the machine is shutting down.  If you register for
a last chance shutdown notification you cannot do the following:
o Call any pageable routines
o Access pageable memory
o Perform any file I/O operations

If you register for a normal shutdown notification, all of these are
available to you.

This function implementation does nothing, but if you had any outstanding
file handles open, this is where you would close them.

Arguments:
Device - The device which registered the notification during init

Return Value:
None

--*/

{
    UNREFERENCED_PARAMETER(Device);
    return;
}

