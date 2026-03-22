/**
 * ECUconnect L2CAP Profile Driver - Connection Management
 *
 * This file handles L2CAP connection lifecycle including:
 * - Connection establishment (BRB_L2CA_OPEN_CHANNEL)
 * - Connection teardown (BRB_L2CA_CLOSE_CHANNEL)
 * - Data transfer (BRB_L2CA_ACL_TRANSFER)
 * - Indication callbacks for connection events
 *
 * Copyright (c) ECUconnect Project
 */

#include "driver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, EcuConnectionCreate)
#endif

/**
 * EcuConnectionCreate - Allocate and initialize a connection object
 */
NTSTATUS
EcuConnectionCreate(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _Out_ PCONNECTION_OBJECT* Connection
)
{
    PCONNECTION_OBJECT connection;

    PAGED_CODE();

    *Connection = NULL;

    // Allocate connection object
    connection = (PCONNECTION_OBJECT)ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(CONNECTION_OBJECT),
        ECUCONNECT_POOL_TAG
    );

    if (!connection) {
        KdPrint(("ECUconnect: Failed to allocate connection object\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Initialize connection
    RtlZeroMemory(connection, sizeof(CONNECTION_OBJECT));
    connection->State = EcuConnectionUninitialized;
    connection->DeviceContext = DeviceContext;
    connection->RefCount = 1;
    connection->Psm = ECUCONNECT_L2CAP_PSM;
    connection->OutMtu = ECUCONNECT_L2CAP_MTU;
    connection->InMtu = ECUCONNECT_L2CAP_MTU;

    // Initialize event for synchronous operations
    KeInitializeEvent(&connection->ConnectEvent, NotificationEvent, FALSE);

    // Allocate receive buffer
    connection->ReceiveBuffer = ExAllocatePoolWithTag(
        NonPagedPool,
        ECUCONNECT_RECEIVE_BUFFER_SIZE,
        ECUCONNECT_POOL_TAG
    );

    if (!connection->ReceiveBuffer) {
        ExFreePoolWithTag(connection, ECUCONNECT_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    connection->ReceiveBufferSize = ECUCONNECT_RECEIVE_BUFFER_SIZE;

    KdPrint(("ECUconnect: Connection object created %p\n", connection));

    *Connection = connection;
    return STATUS_SUCCESS;
}

/**
 * EcuConnectionReference - Add reference to connection
 */
VOID
EcuConnectionReference(
    _In_ PCONNECTION_OBJECT Connection
)
{
    InterlockedIncrement(&Connection->RefCount);
}

/**
 * EcuConnectionDereference - Release reference to connection
 */
VOID
EcuConnectionDereference(
    _In_ PCONNECTION_OBJECT Connection
)
{
    LONG refCount = InterlockedDecrement(&Connection->RefCount);

    if (refCount == 0) {
        KdPrint(("ECUconnect: Freeing connection object %p\n", Connection));

        if (Connection->ReceiveBuffer) {
            ExFreePoolWithTag(Connection->ReceiveBuffer, ECUCONNECT_POOL_TAG);
        }

        ExFreePoolWithTag(Connection, ECUCONNECT_POOL_TAG);
    }
}

/**
 * OpenChannelCompletion - Completion routine for L2CAP channel open
 */
static VOID
OpenChannelCompletion(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT Context
)
{
    PCONNECTION_OBJECT connection = (PCONNECTION_OBJECT)Context;
    NTSTATUS status = Params->IoStatus.Status;
    struct _BRB_L2CA_OPEN_CHANNEL* brb;

    UNREFERENCED_PARAMETER(Target);

    brb = (struct _BRB_L2CA_OPEN_CHANNEL*)&connection->Brb;

    if (NT_SUCCESS(status)) {
        // Extract channel handle and negotiated parameters
        connection->ChannelHandle = brb->ChannelHandle;
        connection->OutMtu = brb->OutResults.Params.Mtu;
        connection->InMtu = brb->InResults.Params.Mtu;
        connection->State = EcuConnectionConnected;

        KdPrint(("ECUconnect: L2CAP channel opened, handle=%p, MTU out=%d in=%d\n",
                 connection->ChannelHandle, connection->OutMtu, connection->InMtu));
    } else {
        connection->State = EcuConnectionFailed;
        KdPrint(("ECUconnect: L2CAP channel open failed 0x%08X\n", status));
    }

    connection->ConnectStatus = status;
    KeSetEvent(&connection->ConnectEvent, IO_NO_INCREMENT, FALSE);

    // Delete the request
    WdfObjectDelete(Request);
}

/**
 * EcuConnectionConnect - Open L2CAP channel to remote device
 */
NTSTATUS
EcuConnectionConnect(
    _In_ PCONNECTION_OBJECT Connection,
    _In_ BTH_ADDR RemoteAddress,
    _In_ USHORT Psm,
    _In_ ULONG TimeoutMs
)
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext = Connection->DeviceContext;
    struct _BRB_L2CA_OPEN_CHANNEL* brb;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFREQUEST request;
    WDFMEMORY memory;
    WDF_REQUEST_SEND_OPTIONS sendOptions;
    LARGE_INTEGER timeout;

    KdPrint(("ECUconnect: Connecting to %012llX PSM %d\n",
             (ULONGLONG)RemoteAddress, Psm));

    // Validate state
    if (Connection->State != EcuConnectionUninitialized &&
        Connection->State != EcuConnectionDisconnected) {
        KdPrint(("ECUconnect: Invalid state for connect: %d\n", Connection->State));
        return STATUS_INVALID_DEVICE_STATE;
    }

    Connection->RemoteAddress = RemoteAddress;
    Connection->Psm = Psm;
    Connection->State = EcuConnectionConnecting;

    // Clear the event
    KeClearEvent(&Connection->ConnectEvent);

    // Initialize BRB for L2CA_OPEN_CHANNEL
    brb = &Connection->Brb.BrbL2caOpenChannel;
    RtlZeroMemory(brb, sizeof(*brb));

    // Set connection parameters
    brb->BtAddress = RemoteAddress;
    brb->Psm = Psm;

    // Configure channel parameters
    brb->ChannelFlags = CF_ROLE_EITHER;
    brb->ConfigOut.Flags = 0;
    brb->ConfigOut.Mtu.Max = Connection->OutMtu;
    brb->ConfigOut.Mtu.Min = 48;  // Minimum L2CAP MTU
    brb->ConfigOut.FlushTO.Max = 0xFFFF;  // Infinite flush timeout
    brb->ConfigOut.FlushTO.Min = 0xFFFF;

    brb->ConfigIn.Flags = 0;
    brb->ConfigIn.Mtu.Max = Connection->InMtu;
    brb->ConfigIn.Mtu.Min = 48;
    brb->ConfigIn.FlushTO.Max = 0xFFFF;
    brb->ConfigIn.FlushTO.Min = 0xFFFF;

    // Set indication callback for disconnect/data events
    brb->CallbackFlags = CALLBACK_DISCONNECT | CALLBACK_RECV_PACKET;
    brb->Callback = EcuL2capIndicationCallback;
    brb->CallbackContext = Connection;
    brb->ReferenceObject = NULL;

    // Create request
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    status = WdfRequestCreate(&attributes, deviceContext->IoTarget, &request);
    if (!NT_SUCCESS(status)) {
        Connection->State = EcuConnectionFailed;
        return status;
    }

    // Create memory for BRB
    status = WdfMemoryCreatePreallocated(
        WDF_NO_OBJECT_ATTRIBUTES,
        brb,
        sizeof(*brb),
        &memory
    );

    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(request);
        Connection->State = EcuConnectionFailed;
        return status;
    }

    // Format the request
    status = WdfIoTargetFormatRequestForInternalIoctl(
        deviceContext->IoTarget,
        request,
        IOCTL_INTERNAL_BTH_SUBMIT_BRB,
        memory,
        NULL,
        memory,
        NULL
    );

    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(memory);
        WdfObjectDelete(request);
        Connection->State = EcuConnectionFailed;
        return status;
    }

    // Set completion routine
    WdfRequestSetCompletionRoutine(
        request,
        OpenChannelCompletion,
        Connection
    );

    // Send the request
    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, 0);
    if (!WdfRequestSend(request, deviceContext->IoTarget, &sendOptions)) {
        status = WdfRequestGetStatus(request);
        WdfObjectDelete(memory);
        WdfObjectDelete(request);
        Connection->State = EcuConnectionFailed;
        return status;
    }

    WdfObjectDelete(memory);

    // Wait for completion with timeout
    timeout.QuadPart = -((LONGLONG)TimeoutMs * 10000);  // Relative time in 100ns units
    status = KeWaitForSingleObject(
        &Connection->ConnectEvent,
        Executive,
        KernelMode,
        FALSE,
        &timeout
    );

    if (status == STATUS_TIMEOUT) {
        KdPrint(("ECUconnect: Connection timeout\n"));
        Connection->State = EcuConnectionFailed;
        return STATUS_TIMEOUT;
    }

    return Connection->ConnectStatus;
}

/**
 * EcuConnectionDisconnect - Close L2CAP channel
 */
NTSTATUS
EcuConnectionDisconnect(
    _In_ PCONNECTION_OBJECT Connection
)
{
    NTSTATUS status;
    BRB brb;

    KdPrint(("ECUconnect: Disconnecting connection %p, state=%d\n",
             Connection, Connection->State));

    // Check if we have a channel to close
    if (Connection->State != EcuConnectionConnected) {
        Connection->State = EcuConnectionDisconnected;
        return STATUS_SUCCESS;
    }

    if (!Connection->ChannelHandle) {
        Connection->State = EcuConnectionDisconnected;
        return STATUS_SUCCESS;
    }

    Connection->State = EcuConnectionDisconnecting;

    // Initialize BRB for close
    RtlZeroMemory(&brb, sizeof(brb));
    brb.BrbL2caCloseChannel.Hdr.Length = sizeof(brb.BrbL2caCloseChannel);
    brb.BrbL2caCloseChannel.Hdr.Type = BRB_L2CA_CLOSE_CHANNEL;
    brb.BrbL2caCloseChannel.ChannelHandle = Connection->ChannelHandle;

    // Send synchronously
    status = EcuBuildAndSendBrbSynchronous(Connection->DeviceContext, &brb);

    Connection->ChannelHandle = NULL;
    Connection->State = EcuConnectionDisconnected;

    if (!NT_SUCCESS(status)) {
        KdPrint(("ECUconnect: Close channel failed 0x%08X\n", status));
    } else {
        KdPrint(("ECUconnect: Channel closed\n"));
    }

    return status;
}

/**
 * EcuL2capIndicationCallback - Handle L2CAP events
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
EcuL2capIndicationCallback(
    _In_ PVOID Context,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters
)
{
    PCONNECTION_OBJECT connection = (PCONNECTION_OBJECT)Context;

    KdPrint(("ECUconnect: L2CAP indication %d\n", Indication));

    switch (Indication) {
    case IndicationRemoteDisconnect:
        KdPrint(("ECUconnect: Remote disconnect, reason=%d\n",
                 Parameters->Parameters.Disconnect.Reason));
        connection->State = EcuConnectionDisconnected;
        connection->ChannelHandle = NULL;
        break;

    case IndicationRecvPacket:
        // Data received on channel - this is handled via continuous reader
        // or synchronous reads, so we just log it here
        KdPrint(("ECUconnect: Received packet notification\n"));
        break;

    default:
        KdPrint(("ECUconnect: Unhandled indication %d\n", Indication));
        break;
    }
}

/**
 * EcuBuildAndSendBrb - Send BRB asynchronously with completion routine
 */
NTSTATUS
EcuBuildAndSendBrb(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ PBRB Brb,
    _In_ PFN_WDF_REQUEST_COMPLETION_ROUTINE CompletionRoutine,
    _In_opt_ PVOID CompletionContext
)
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFREQUEST request;
    WDFMEMORY memory;
    WDF_REQUEST_SEND_OPTIONS sendOptions;

    // Create request
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    status = WdfRequestCreate(&attributes, DeviceContext->IoTarget, &request);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Create memory for BRB
    status = WdfMemoryCreatePreallocated(
        WDF_NO_OBJECT_ATTRIBUTES,
        Brb,
        Brb->BrbHeader.Length,
        &memory
    );

    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(request);
        return status;
    }

    // Format the request
    status = WdfIoTargetFormatRequestForInternalIoctl(
        DeviceContext->IoTarget,
        request,
        IOCTL_INTERNAL_BTH_SUBMIT_BRB,
        memory,
        NULL,
        memory,
        NULL
    );

    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(memory);
        WdfObjectDelete(request);
        return status;
    }

    // Set completion routine
    WdfRequestSetCompletionRoutine(request, CompletionRoutine, CompletionContext);

    // Send the request
    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, 0);
    if (!WdfRequestSend(request, DeviceContext->IoTarget, &sendOptions)) {
        status = WdfRequestGetStatus(request);
        WdfObjectDelete(memory);
        WdfObjectDelete(request);
        return status;
    }

    WdfObjectDelete(memory);
    return STATUS_PENDING;
}
