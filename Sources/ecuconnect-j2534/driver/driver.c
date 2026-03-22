/**
 * ECUconnect L2CAP Profile Driver - Driver Entry
 *
 * This file contains the driver entry point and initialization code.
 *
 * Copyright (c) ECUconnect Project
 */

#include "driver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, EcuEvtDeviceAdd)
#pragma alloc_text(PAGE, EcuEvtDriverUnload)
#pragma alloc_text(PAGE, EcuEvtDevicePrepareHardware)
#pragma alloc_text(PAGE, EcuEvtDeviceReleaseHardware)
#pragma alloc_text(PAGE, EcuEvtDeviceSelfManagedIoInit)
#pragma alloc_text(PAGE, EcuEvtDeviceSelfManagedIoCleanup)
#endif

/**
 * DriverEntry - Driver initialization
 */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    KdPrint(("ECUconnect L2CAP Driver v%d.%d\n",
             ECUCONNECT_DRIVER_VERSION_MAJOR,
             ECUCONNECT_DRIVER_VERSION_MINOR));

    // Initialize driver configuration
    WDF_DRIVER_CONFIG_INIT(&config, EcuEvtDeviceAdd);
    config.EvtDriverUnload = EcuEvtDriverUnload;

    // Set driver object attributes
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    // Create the WDF driver object
    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        WDF_NO_HANDLE
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("ECUconnect: WdfDriverCreate failed 0x%08X\n", status));
        return status;
    }

    KdPrint(("ECUconnect: Driver initialized successfully\n"));
    return STATUS_SUCCESS;
}

/**
 * EcuEvtDeviceAdd - Called when PnP manager finds a matching device
 */
NTSTATUS
EcuEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    NTSTATUS status;
    WDFDEVICE device;
    PDEVICE_CONTEXT deviceContext;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFQUEUE queue;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    KdPrint(("ECUconnect: EcuEvtDeviceAdd\n"));

    // Configure PnP/Power callbacks
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = EcuEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = EcuEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = EcuEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = EcuEvtDeviceD0Exit;
    pnpPowerCallbacks.EvtDeviceSelfManagedIoInit = EcuEvtDeviceSelfManagedIoInit;
    pnpPowerCallbacks.EvtDeviceSelfManagedIoCleanup = EcuEvtDeviceSelfManagedIoCleanup;

    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    // Configure file object callbacks
    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        EcuEvtDeviceFileCreate,
        EcuEvtFileClose,
        EcuEvtFileCleanup
    );

    // Allocate file context
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, FILE_CONTEXT);
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, &deviceAttributes);

    // Set device characteristics
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_ECUCONNECT);
    WdfDeviceInitSetExclusive(DeviceInit, FALSE);  // Allow multiple handles
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoBuffered);

    // Create the device
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);

    if (!NT_SUCCESS(status)) {
        KdPrint(("ECUconnect: WdfDeviceCreate failed 0x%08X\n", status));
        return status;
    }

    // Initialize device context
    deviceContext = DeviceGetContext(device);
    RtlZeroMemory(deviceContext, sizeof(DEVICE_CONTEXT));
    deviceContext->Device = device;

    // Create spin lock for connection management
    WDF_OBJECT_ATTRIBUTES_INIT(&deviceAttributes);
    deviceAttributes.ParentObject = device;
    status = WdfSpinLockCreate(&deviceAttributes, &deviceContext->ConnectionLock);
    if (!NT_SUCCESS(status)) {
        KdPrint(("ECUconnect: WdfSpinLockCreate failed 0x%08X\n", status));
        return status;
    }

    // Create default I/O queue for read/write/ioctl
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoRead = EcuEvtIoRead;
    queueConfig.EvtIoWrite = EcuEvtIoWrite;
    queueConfig.EvtIoDeviceControl = EcuEvtIoDeviceControl;

    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) {
        KdPrint(("ECUconnect: WdfIoQueueCreate failed 0x%08X\n", status));
        return status;
    }

    // Create device interface for user-mode access
    status = WdfDeviceCreateDeviceInterface(
        device,
        &GUID_DEVINTERFACE_ECUCONNECT_L2CAP,
        NULL
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("ECUconnect: WdfDeviceCreateDeviceInterface failed 0x%08X\n", status));
        return status;
    }

    KdPrint(("ECUconnect: Device created successfully\n"));
    return STATUS_SUCCESS;
}

/**
 * EcuEvtDriverUnload - Driver cleanup
 */
VOID
EcuEvtDriverUnload(
    _In_ WDFDRIVER Driver
)
{
    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    KdPrint(("ECUconnect: Driver unloading\n"));
}

/**
 * EcuEvtDevicePrepareHardware - Hardware initialization
 */
NTSTATUS
EcuEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
)
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    KdPrint(("ECUconnect: EcuEvtDevicePrepareHardware\n"));

    deviceContext = DeviceGetContext(Device);

    // Get the default I/O target (Bluetooth stack)
    deviceContext->IoTarget = WdfDeviceGetIoTarget(Device);

    // Query the Bluetooth profile driver interface
    status = EcuQueryProfileDriverInterface(deviceContext);
    if (!NT_SUCCESS(status)) {
        KdPrint(("ECUconnect: Failed to query profile driver interface 0x%08X\n", status));
        return status;
    }

    // Get local radio information
    status = EcuGetLocalRadioInfo(deviceContext);
    if (!NT_SUCCESS(status)) {
        KdPrint(("ECUconnect: Failed to get local radio info 0x%08X\n", status));
        // Non-fatal - continue initialization
    }

    return STATUS_SUCCESS;
}

/**
 * EcuEvtDeviceReleaseHardware - Hardware cleanup
 */
NTSTATUS
EcuEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
)
{
    PDEVICE_CONTEXT deviceContext;

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    KdPrint(("ECUconnect: EcuEvtDeviceReleaseHardware\n"));

    deviceContext = DeviceGetContext(Device);

    // Clean up any active connections
    if (deviceContext->ActiveConnection) {
        EcuConnectionDisconnect(deviceContext->ActiveConnection);
        EcuConnectionDereference(deviceContext->ActiveConnection);
        deviceContext->ActiveConnection = NULL;
    }

    return STATUS_SUCCESS;
}

/**
 * EcuEvtDeviceD0Entry - Device powered on
 */
NTSTATUS
EcuEvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(PreviousState);

    KdPrint(("ECUconnect: EcuEvtDeviceD0Entry from state %d\n", PreviousState));
    return STATUS_SUCCESS;
}

/**
 * EcuEvtDeviceD0Exit - Device powering down
 */
NTSTATUS
EcuEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(TargetState);

    KdPrint(("ECUconnect: EcuEvtDeviceD0Exit to state %d\n", TargetState));
    return STATUS_SUCCESS;
}

/**
 * EcuEvtDeviceSelfManagedIoInit - Called after device enters D0
 */
NTSTATUS
EcuEvtDeviceSelfManagedIoInit(
    _In_ WDFDEVICE Device
)
{
    UNREFERENCED_PARAMETER(Device);

    PAGED_CODE();

    KdPrint(("ECUconnect: EcuEvtDeviceSelfManagedIoInit\n"));

    // Device is ready for I/O
    // Note: We don't register as an L2CAP server - we're a client-only driver
    // that connects to ECUconnect devices on demand

    return STATUS_SUCCESS;
}

/**
 * EcuEvtDeviceSelfManagedIoCleanup - Called before device leaves D0
 */
VOID
EcuEvtDeviceSelfManagedIoCleanup(
    _In_ WDFDEVICE Device
)
{
    UNREFERENCED_PARAMETER(Device);

    PAGED_CODE();

    KdPrint(("ECUconnect: EcuEvtDeviceSelfManagedIoCleanup\n"));
}

/**
 * EcuQueryProfileDriverInterface - Get Bluetooth DDI interface
 */
NTSTATUS
EcuQueryProfileDriverInterface(
    _In_ PDEVICE_CONTEXT DeviceContext
)
{
    NTSTATUS status;

    KdPrint(("ECUconnect: Querying profile driver interface\n"));

    status = WdfFdoQueryForInterface(
        DeviceContext->Device,
        &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE,
        (PINTERFACE)&DeviceContext->ProfileDriverInterface,
        sizeof(BTH_PROFILE_DRIVER_INTERFACE),
        BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI,
        NULL
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("ECUconnect: WdfFdoQueryForInterface failed 0x%08X\n", status));
        return status;
    }

    KdPrint(("ECUconnect: Profile driver interface obtained\n"));
    return STATUS_SUCCESS;
}

/**
 * EcuGetLocalRadioInfo - Get local Bluetooth adapter information
 */
NTSTATUS
EcuGetLocalRadioInfo(
    _In_ PDEVICE_CONTEXT DeviceContext
)
{
    NTSTATUS status;
    BRB brb;

    RtlZeroMemory(&brb, sizeof(brb));

    // Initialize BRB header
    brb.BrbGetLocalBdAddress.Hdr.Length = sizeof(brb.BrbGetLocalBdAddress);
    brb.BrbGetLocalBdAddress.Hdr.Type = BRB_HCI_GET_LOCAL_BD_ADDR;

    status = EcuBuildAndSendBrbSynchronous(DeviceContext, &brb);

    if (NT_SUCCESS(status)) {
        DeviceContext->LocalAddress = brb.BrbGetLocalBdAddress.BtAddress;
        KdPrint(("ECUconnect: Local BT address: %012llX\n",
                 (ULONGLONG)DeviceContext->LocalAddress));
    }

    return status;
}

/**
 * EcuBuildAndSendBrbSynchronous - Send BRB and wait for completion
 */
NTSTATUS
EcuBuildAndSendBrbSynchronous(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ PBRB Brb
)
{
    NTSTATUS status;
    WDF_MEMORY_DESCRIPTOR memDesc;
    WDFMEMORY memory;
    WDF_OBJECT_ATTRIBUTES attributes;

    // Create memory for IOCTL output
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = DeviceContext->Device;

    status = WdfMemoryCreatePreallocated(
        &attributes,
        Brb,
        Brb->BrbHeader.Length,
        &memory
    );

    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&memDesc, memory, NULL);

    // Send the BRB via IOCTL
    status = WdfIoTargetSendIoctlSynchronously(
        DeviceContext->IoTarget,
        NULL,
        IOCTL_INTERNAL_BTH_SUBMIT_BRB,
        &memDesc,
        &memDesc,
        NULL,
        NULL
    );

    WdfObjectDelete(memory);

    return status;
}
