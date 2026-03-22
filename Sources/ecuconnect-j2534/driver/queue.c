/**
 * ECUconnect L2CAP Profile Driver - I/O Queue Handling
 *
 * This file implements the I/O queue callbacks for:
 * - Read operations (receive data from L2CAP channel)
 * - Write operations (send data to L2CAP channel)
 * - Device control operations (connect, disconnect, status)
 *
 * Copyright (c) ECUconnect Project
 */

#include "driver.h"

/**
 * Read Completion Routine
 */
static VOID
ReadCompletion(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT Context
)
{
    NTSTATUS status = Params->IoStatus.Status;
    PBRB_CONTEXT brbContext = (PBRB_CONTEXT)Context;
    struct _BRB_L2CA_ACL_TRANSFER* brb;
    ULONG bytesRead = 0;

    UNREFERENCED_PARAMETER(Target);

    brb = (struct _BRB_L2CA_ACL_TRANSFER*)&brbContext->Brb;

    if (NT_SUCCESS(status)) {
        bytesRead = brb->BufferSize;
        KdPrint(("ECUconnect: Read completed, %d bytes\n", bytesRead));
    } else {
        KdPrint(("ECUconnect: Read failed 0x%08X\n", status));
    }

    // Complete the original request
    WdfRequestCompleteWithInformation(brbContext->Request, status, bytesRead);

    // Dereference connection
    if (brbContext->Connection) {
        EcuConnectionDereference(brbContext->Connection);
    }
}

/**
 * Write Completion Routine
 */
static VOID
WriteCompletion(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT Context
)
{
    NTSTATUS status = Params->IoStatus.Status;
    PBRB_CONTEXT brbContext = (PBRB_CONTEXT)Context;
    struct _BRB_L2CA_ACL_TRANSFER* brb;
    ULONG bytesWritten = 0;

    UNREFERENCED_PARAMETER(Target);

    brb = (struct _BRB_L2CA_ACL_TRANSFER*)&brbContext->Brb;

    if (NT_SUCCESS(status)) {
        bytesWritten = brb->BufferSize;
        KdPrint(("ECUconnect: Write completed, %d bytes\n", bytesWritten));
    } else {
        KdPrint(("ECUconnect: Write failed 0x%08X\n", status));
    }

    // Complete the original request
    WdfRequestCompleteWithInformation(brbContext->Request, status, bytesWritten);

    // Dereference connection
    if (brbContext->Connection) {
        EcuConnectionDereference(brbContext->Connection);
    }
}

/**
 * EcuEvtIoRead - Handle read requests from user-mode
 */
VOID
EcuEvtIoRead(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t Length
)
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext;
    PFILE_CONTEXT fileContext;
    PCONNECTION_OBJECT connection;
    PVOID buffer;
    size_t bufferSize;
    WDFREQUEST brbRequest;
    WDFMEMORY brbMemory;
    WDF_OBJECT_ATTRIBUTES attributes;
    PBRB_CONTEXT brbContext;
    struct _BRB_L2CA_ACL_TRANSFER* brb;
    WDF_REQUEST_SEND_OPTIONS sendOptions;

    UNREFERENCED_PARAMETER(Queue);

    KdPrint(("ECUconnect: EcuEvtIoRead, length=%llu\n", (ULONGLONG)Length));

    deviceContext = DeviceGetContext(WdfIoQueueGetDevice(Queue));
    fileContext = FileGetContext(WdfRequestGetFileObject(Request));

    // Get connection from file context
    connection = fileContext->Connection;
    if (!connection || connection->State != EcuConnectionConnected) {
        KdPrint(("ECUconnect: Read failed - not connected\n"));
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_CONNECTED);
        return;
    }

    // Get output buffer
    status = WdfRequestRetrieveOutputBuffer(Request, 1, &buffer, &bufferSize);
    if (!NT_SUCCESS(status)) {
        KdPrint(("ECUconnect: Failed to get output buffer 0x%08X\n", status));
        WdfRequestComplete(Request, status);
        return;
    }

    // Create BRB request with context
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, BRB_CONTEXT);
    status = WdfRequestCreate(&attributes, deviceContext->IoTarget, &brbRequest);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    brbContext = BrbGetContext(brbRequest);
    brbContext->Request = Request;
    brbContext->Connection = connection;
    brbContext->Buffer = buffer;
    brbContext->BufferSize = (ULONG)bufferSize;

    // Reference connection
    EcuConnectionReference(connection);

    // Initialize BRB for read
    brb = (struct _BRB_L2CA_ACL_TRANSFER*)&brbContext->Brb;
    RtlZeroMemory(brb, sizeof(*brb));
    brb->Hdr.Length = sizeof(*brb);
    brb->Hdr.Type = BRB_L2CA_ACL_TRANSFER;

    brb->ChannelHandle = connection->ChannelHandle;
    brb->TransferFlags = ACL_TRANSFER_DIRECTION_IN | ACL_SHORT_TRANSFER_OK;
    brb->Buffer = buffer;
    brb->BufferSize = (ULONG)min(bufferSize, connection->InMtu);

    // Create memory for BRB
    status = WdfMemoryCreatePreallocated(
        WDF_NO_OBJECT_ATTRIBUTES,
        brb,
        sizeof(*brb),
        &brbMemory
    );

    if (!NT_SUCCESS(status)) {
        EcuConnectionDereference(connection);
        WdfObjectDelete(brbRequest);
        WdfRequestComplete(Request, status);
        return;
    }

    // Format request
    status = WdfIoTargetFormatRequestForInternalIoctl(
        deviceContext->IoTarget,
        brbRequest,
        IOCTL_INTERNAL_BTH_SUBMIT_BRB,
        brbMemory,
        NULL,
        brbMemory,
        NULL
    );

    if (!NT_SUCCESS(status)) {
        EcuConnectionDereference(connection);
        WdfObjectDelete(brbMemory);
        WdfObjectDelete(brbRequest);
        WdfRequestComplete(Request, status);
        return;
    }

    // Set completion routine
    WdfRequestSetCompletionRoutine(brbRequest, ReadCompletion, brbContext);

    // Send request
    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, 0);
    if (!WdfRequestSend(brbRequest, deviceContext->IoTarget, &sendOptions)) {
        status = WdfRequestGetStatus(brbRequest);
        EcuConnectionDereference(connection);
        WdfObjectDelete(brbMemory);
        WdfObjectDelete(brbRequest);
        WdfRequestComplete(Request, status);
    }

    WdfObjectDelete(brbMemory);
}

/**
 * EcuEvtIoWrite - Handle write requests from user-mode
 */
VOID
EcuEvtIoWrite(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t Length
)
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext;
    PFILE_CONTEXT fileContext;
    PCONNECTION_OBJECT connection;
    PVOID buffer;
    size_t bufferSize;
    WDFREQUEST brbRequest;
    WDFMEMORY brbMemory;
    WDF_OBJECT_ATTRIBUTES attributes;
    PBRB_CONTEXT brbContext;
    struct _BRB_L2CA_ACL_TRANSFER* brb;
    WDF_REQUEST_SEND_OPTIONS sendOptions;

    UNREFERENCED_PARAMETER(Queue);

    KdPrint(("ECUconnect: EcuEvtIoWrite, length=%llu\n", (ULONGLONG)Length));

    deviceContext = DeviceGetContext(WdfIoQueueGetDevice(Queue));
    fileContext = FileGetContext(WdfRequestGetFileObject(Request));

    // Get connection from file context
    connection = fileContext->Connection;
    if (!connection || connection->State != EcuConnectionConnected) {
        KdPrint(("ECUconnect: Write failed - not connected\n"));
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_CONNECTED);
        return;
    }

    // Get input buffer
    status = WdfRequestRetrieveInputBuffer(Request, 1, &buffer, &bufferSize);
    if (!NT_SUCCESS(status)) {
        KdPrint(("ECUconnect: Failed to get input buffer 0x%08X\n", status));
        WdfRequestComplete(Request, status);
        return;
    }

    // Check MTU
    if (bufferSize > connection->OutMtu) {
        KdPrint(("ECUconnect: Write size %llu exceeds MTU %d\n",
                 (ULONGLONG)bufferSize, connection->OutMtu));
        WdfRequestComplete(Request, STATUS_BUFFER_OVERFLOW);
        return;
    }

    // Create BRB request with context
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, BRB_CONTEXT);
    status = WdfRequestCreate(&attributes, deviceContext->IoTarget, &brbRequest);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    brbContext = BrbGetContext(brbRequest);
    brbContext->Request = Request;
    brbContext->Connection = connection;
    brbContext->Buffer = buffer;
    brbContext->BufferSize = (ULONG)bufferSize;

    // Reference connection
    EcuConnectionReference(connection);

    // Initialize BRB for write
    brb = (struct _BRB_L2CA_ACL_TRANSFER*)&brbContext->Brb;
    RtlZeroMemory(brb, sizeof(*brb));
    brb->Hdr.Length = sizeof(*brb);
    brb->Hdr.Type = BRB_L2CA_ACL_TRANSFER;

    brb->ChannelHandle = connection->ChannelHandle;
    brb->TransferFlags = ACL_TRANSFER_DIRECTION_OUT;
    brb->Buffer = buffer;
    brb->BufferSize = (ULONG)bufferSize;

    // Create memory for BRB
    status = WdfMemoryCreatePreallocated(
        WDF_NO_OBJECT_ATTRIBUTES,
        brb,
        sizeof(*brb),
        &brbMemory
    );

    if (!NT_SUCCESS(status)) {
        EcuConnectionDereference(connection);
        WdfObjectDelete(brbRequest);
        WdfRequestComplete(Request, status);
        return;
    }

    // Format request
    status = WdfIoTargetFormatRequestForInternalIoctl(
        deviceContext->IoTarget,
        brbRequest,
        IOCTL_INTERNAL_BTH_SUBMIT_BRB,
        brbMemory,
        NULL,
        brbMemory,
        NULL
    );

    if (!NT_SUCCESS(status)) {
        EcuConnectionDereference(connection);
        WdfObjectDelete(brbMemory);
        WdfObjectDelete(brbRequest);
        WdfRequestComplete(Request, status);
        return;
    }

    // Set completion routine
    WdfRequestSetCompletionRoutine(brbRequest, WriteCompletion, brbContext);

    // Send request
    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, 0);
    if (!WdfRequestSend(brbRequest, deviceContext->IoTarget, &sendOptions)) {
        status = WdfRequestGetStatus(brbRequest);
        EcuConnectionDereference(connection);
        WdfObjectDelete(brbMemory);
        WdfObjectDelete(brbRequest);
        WdfRequestComplete(Request, status);
    }

    WdfObjectDelete(brbMemory);
}

/**
 * HandleConnectIoctl - Process IOCTL_ECUCONNECT_CONNECT
 */
static NTSTATUS
HandleConnectIoctl(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ PFILE_CONTEXT FileContext,
    _In_ WDFREQUEST Request
)
{
    NTSTATUS status;
    PECUCONNECT_CONNECT_REQUEST connectRequest;
    size_t inputLength;
    PCONNECTION_OBJECT connection;

    status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(ECUCONNECT_CONNECT_REQUEST),
        (PVOID*)&connectRequest,
        &inputLength
    );

    if (!NT_SUCCESS(status)) {
        return status;
    }

    KdPrint(("ECUconnect: Connect request to %012llX PSM %d\n",
             connectRequest->BluetoothAddress, connectRequest->Psm));

    // Check if already connected
    if (FileContext->Connection &&
        FileContext->Connection->State == EcuConnectionConnected) {
        return STATUS_CONNECTION_ACTIVE;
    }

    // Create new connection if needed
    if (!FileContext->Connection) {
        status = EcuConnectionCreate(DeviceContext, &connection);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        FileContext->Connection = connection;
    } else {
        connection = FileContext->Connection;
    }

    // Connect
    status = EcuConnectionConnect(
        connection,
        (BTH_ADDR)connectRequest->BluetoothAddress,
        connectRequest->Psm ? connectRequest->Psm : ECUCONNECT_L2CAP_PSM,
        connectRequest->TimeoutMs ? connectRequest->TimeoutMs : 10000
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("ECUconnect: Connection failed 0x%08X\n", status));
    }

    return status;
}

/**
 * HandleDisconnectIoctl - Process IOCTL_ECUCONNECT_DISCONNECT
 */
static NTSTATUS
HandleDisconnectIoctl(
    _In_ PFILE_CONTEXT FileContext
)
{
    if (!FileContext->Connection) {
        return STATUS_SUCCESS;
    }

    return EcuConnectionDisconnect(FileContext->Connection);
}

/**
 * HandleGetStatusIoctl - Process IOCTL_ECUCONNECT_GET_STATUS
 */
static NTSTATUS
HandleGetStatusIoctl(
    _In_ PFILE_CONTEXT FileContext,
    _In_ WDFREQUEST Request
)
{
    NTSTATUS status;
    PECUCONNECT_STATUS outputStatus;
    size_t outputLength;
    PCONNECTION_OBJECT connection = FileContext->Connection;

    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(ECUCONNECT_STATUS),
        (PVOID*)&outputStatus,
        &outputLength
    );

    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlZeroMemory(outputStatus, sizeof(ECUCONNECT_STATUS));

    if (connection) {
        outputStatus->State = connection->State;
        outputStatus->RemoteAddress = (ULONGLONG)connection->RemoteAddress;
        outputStatus->NegotiatedMtu = connection->OutMtu;
    } else {
        outputStatus->State = EcuConnectionUninitialized;
    }

    WdfRequestSetInformation(Request, sizeof(ECUCONNECT_STATUS));
    return STATUS_SUCCESS;
}

/**
 * EcuEvtIoDeviceControl - Handle IOCTLs from user-mode
 */
VOID
EcuEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
)
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext;
    PFILE_CONTEXT fileContext;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    KdPrint(("ECUconnect: EcuEvtIoDeviceControl, code=0x%08X\n", IoControlCode));

    deviceContext = DeviceGetContext(WdfIoQueueGetDevice(Queue));
    fileContext = FileGetContext(WdfRequestGetFileObject(Request));

    switch (IoControlCode) {
    case IOCTL_ECUCONNECT_CONNECT:
        status = HandleConnectIoctl(deviceContext, fileContext, Request);
        break;

    case IOCTL_ECUCONNECT_DISCONNECT:
        status = HandleDisconnectIoctl(fileContext);
        break;

    case IOCTL_ECUCONNECT_GET_STATUS:
        status = HandleGetStatusIoctl(fileContext, Request);
        WdfRequestComplete(Request, status);
        return;  // Already set information

    default:
        KdPrint(("ECUconnect: Unknown IOCTL 0x%08X\n", IoControlCode));
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestComplete(Request, status);
}

/**
 * EcuEvtDeviceFileCreate - Handle file open from user-mode
 */
VOID
EcuEvtDeviceFileCreate(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject
)
{
    PDEVICE_CONTEXT deviceContext;
    PFILE_CONTEXT fileContext;

    KdPrint(("ECUconnect: EcuEvtDeviceFileCreate\n"));

    deviceContext = DeviceGetContext(Device);
    fileContext = FileGetContext(FileObject);

    // Initialize file context
    RtlZeroMemory(fileContext, sizeof(FILE_CONTEXT));
    fileContext->DeviceContext = deviceContext;
    fileContext->Connection = NULL;

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

/**
 * EcuEvtFileClose - Handle file close from user-mode
 */
VOID
EcuEvtFileClose(
    _In_ WDFFILEOBJECT FileObject
)
{
    KdPrint(("ECUconnect: EcuEvtFileClose\n"));

    UNREFERENCED_PARAMETER(FileObject);
    // Cleanup is done in EcuEvtFileCleanup
}

/**
 * EcuEvtFileCleanup - Handle file cleanup
 */
VOID
EcuEvtFileCleanup(
    _In_ WDFFILEOBJECT FileObject
)
{
    PFILE_CONTEXT fileContext;

    KdPrint(("ECUconnect: EcuEvtFileCleanup\n"));

    fileContext = FileGetContext(FileObject);

    // Disconnect if connected
    if (fileContext->Connection) {
        if (fileContext->Connection->State == EcuConnectionConnected) {
            EcuConnectionDisconnect(fileContext->Connection);
        }
        EcuConnectionDereference(fileContext->Connection);
        fileContext->Connection = NULL;
    }
}
