/**
 * ECUconnect L2CAP Profile Driver - Internal Header
 *
 * This driver implements a Bluetooth L2CAP profile for communicating with
 * ECUconnect devices using the CANyonero protocol over L2CAP PSM 129.
 *
 * Architecture:
 * - Registers as a Bluetooth profile driver
 * - Creates a device interface for user-mode access
 * - Handles L2CAP channel establishment and data transfer
 * - Exposes Read/Write for streaming data to/from the device
 *
 * Copyright (c) ECUconnect Project
 */

#ifndef ECUCONNECT_L2CAP_DRIVER_H
#define ECUCONNECT_L2CAP_DRIVER_H

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#include <bthdef.h>
#include <bthguid.h>
#include <bthioctl.h>
#include <sdpnode.h>
#include <bthddi.h>
#include <bthsdpddi.h>
#include <bthsdpdef.h>

#include "public.h"

/**
 * Driver Version
 */
#define ECUCONNECT_DRIVER_VERSION_MAJOR     1
#define ECUCONNECT_DRIVER_VERSION_MINOR     0

/**
 * Memory Pool Tags
 */
#define ECUCONNECT_POOL_TAG                 'UCCE'   // 'ECUC' in little-endian

/**
 * Receive buffer configuration
 */
#define ECUCONNECT_RECEIVE_BUFFER_SIZE      4096
#define ECUCONNECT_RECEIVE_BUFFER_COUNT     4

/**
 * Forward declarations
 */
typedef struct _DEVICE_CONTEXT DEVICE_CONTEXT, *PDEVICE_CONTEXT;
typedef struct _FILE_CONTEXT FILE_CONTEXT, *PFILE_CONTEXT;
typedef struct _CONNECTION_OBJECT CONNECTION_OBJECT, *PCONNECTION_OBJECT;

/**
 * Device Context
 * Stores per-device state including Bluetooth profile interface
 */
typedef struct _DEVICE_CONTEXT {
    // WDFDEVICE owner
    WDFDEVICE Device;

    // Target device for BRB requests
    WDFIOTARGET IoTarget;

    // Bluetooth profile driver interface
    BTH_PROFILE_DRIVER_INTERFACE ProfileDriverInterface;

    // Local Bluetooth radio information
    BTH_LOCAL_RADIO_INFO LocalRadioInfo;
    BTH_ADDR LocalAddress;

    // Continuous reader for incoming L2CAP data
    WDFUSBPIPE ContinuousReaderPipe;    // Not actually USB, but we reuse the pattern

    // Connection management
    WDFSPINLOCK ConnectionLock;
    PCONNECTION_OBJECT ActiveConnection;

    // Statistics
    ULONG TotalBytesSent;
    ULONG TotalBytesReceived;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

/**
 * File Context
 * Stores per-handle state for user-mode applications
 */
typedef struct _FILE_CONTEXT {
    // Parent device
    PDEVICE_CONTEXT DeviceContext;

    // Connection associated with this file handle
    PCONNECTION_OBJECT Connection;

    // Receive queue for this file
    WDFQUEUE ReadQueue;

    // Pending read requests
    LIST_ENTRY PendingReadList;
    WDFSPINLOCK ReadListLock;

} FILE_CONTEXT, *PFILE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FILE_CONTEXT, FileGetContext)

/**
 * Connection Object
 * Manages an L2CAP connection to a remote device
 */
typedef struct _CONNECTION_OBJECT {
    // Connection state machine
    ECUCONNECT_CONNECTION_STATE State;

    // Remote device information
    BTH_ADDR RemoteAddress;
    USHORT Psm;

    // L2CAP channel handle (from BRB_L2CA_OPEN_CHANNEL)
    L2CAP_CHANNEL_HANDLE ChannelHandle;

    // Negotiated parameters
    USHORT OutMtu;      // MTU for outgoing data
    USHORT InMtu;       // MTU for incoming data

    // Back-reference to device context
    PDEVICE_CONTEXT DeviceContext;

    // BRB for pending operations
    BRB Brb;

    // Event for synchronous operations
    KEVENT ConnectEvent;
    NTSTATUS ConnectStatus;

    // Receive buffer management
    PVOID ReceiveBuffer;
    ULONG ReceiveBufferSize;
    ULONG ReceiveDataLength;
    WDFSPINLOCK ReceiveLock;

    // Receive completion queue
    WDFQUEUE ReceiveQueue;

    // Reference count
    LONG RefCount;

} CONNECTION_OBJECT, *PCONNECTION_OBJECT;

/**
 * BRB Context for async operations
 */
typedef struct _BRB_CONTEXT {
    BRB Brb;
    PCONNECTION_OBJECT Connection;
    WDFREQUEST Request;
    PVOID Buffer;
    ULONG BufferSize;
} BRB_CONTEXT, *PBRB_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(BRB_CONTEXT, BrbGetContext)

// ============================================================================
// Driver Entry Points
// ============================================================================

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD EcuEvtDeviceAdd;
EVT_WDF_DRIVER_UNLOAD EcuEvtDriverUnload;

// ============================================================================
// Device Callbacks
// ============================================================================

EVT_WDF_DEVICE_PREPARE_HARDWARE EcuEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE EcuEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY EcuEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT EcuEvtDeviceD0Exit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT EcuEvtDeviceSelfManagedIoInit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP EcuEvtDeviceSelfManagedIoCleanup;

// ============================================================================
// File Object Callbacks
// ============================================================================

EVT_WDF_DEVICE_FILE_CREATE EcuEvtDeviceFileCreate;
EVT_WDF_FILE_CLOSE EcuEvtFileClose;
EVT_WDF_FILE_CLEANUP EcuEvtFileCleanup;

// ============================================================================
// I/O Queue Callbacks
// ============================================================================

EVT_WDF_IO_QUEUE_IO_READ EcuEvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE EcuEvtIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EcuEvtIoDeviceControl;

// ============================================================================
// Connection Management Functions
// ============================================================================

NTSTATUS
EcuConnectionCreate(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _Out_ PCONNECTION_OBJECT* Connection
);

VOID
EcuConnectionReference(
    _In_ PCONNECTION_OBJECT Connection
);

VOID
EcuConnectionDereference(
    _In_ PCONNECTION_OBJECT Connection
);

NTSTATUS
EcuConnectionConnect(
    _In_ PCONNECTION_OBJECT Connection,
    _In_ BTH_ADDR RemoteAddress,
    _In_ USHORT Psm,
    _In_ ULONG TimeoutMs
);

NTSTATUS
EcuConnectionDisconnect(
    _In_ PCONNECTION_OBJECT Connection
);

// ============================================================================
// L2CAP Callback Functions
// ============================================================================

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
EcuL2capIndicationCallback(
    _In_ PVOID Context,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters
);

// ============================================================================
// BRB Helper Functions
// ============================================================================

NTSTATUS
EcuBuildAndSendBrb(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ PBRB Brb,
    _In_ PFN_WDF_REQUEST_COMPLETION_ROUTINE CompletionRoutine,
    _In_opt_ PVOID CompletionContext
);

NTSTATUS
EcuBuildAndSendBrbSynchronous(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ PBRB Brb
);

// ============================================================================
// Utility Functions
// ============================================================================

NTSTATUS
EcuGetLocalRadioInfo(
    _In_ PDEVICE_CONTEXT DeviceContext
);

NTSTATUS
EcuQueryProfileDriverInterface(
    _In_ PDEVICE_CONTEXT DeviceContext
);

#endif // ECUCONNECT_L2CAP_DRIVER_H
