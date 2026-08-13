#include <ntifs.h>
#include <ntddkbd.h>
#include <kbdmou.h>
#include <wdmsec.h>
#include "quickcopy_driver.h"

#define QC_CONTROL_DEVICE L"\\Device\\QuickCopyKeyboardFilter"
#define QC_DOS_DEVICE L"\\DosDevices\\QuickCopyKeyboardFilter"
#define QC_POOL_TAG 'kCcQ'

static const GUID g_QcDeviceClass =
    { 0x928947c4, 0xe022, 0x4f0b, { 0xa5, 0x17, 0x8c, 0x9b, 0x31, 0x92, 0x42, 0x10 } };

typedef enum _QC_DEVICE_KIND {
    QcDeviceControl,
    QcDeviceFilter
} QC_DEVICE_KIND;

typedef struct _QC_DEVICE_EXTENSION {
    QC_DEVICE_KIND Kind;
    PDEVICE_OBJECT LowerDevice;
    CONNECT_DATA UpperConnect;
} QC_DEVICE_EXTENSION, *PQC_DEVICE_EXTENSION;

static KSPIN_LOCK g_StateLock;
static QC_DRIVER_BINDING g_Binding;
static ULONG g_Modifiers;
static BOOLEAN g_TriggerHeld;
static PKEVENT g_TriggerEvent;
static PDEVICE_OBJECT g_ControlDevice;

DRIVER_INITIALIZE DriverEntry;
DRIVER_ADD_DEVICE QcAddDevice;
DRIVER_UNLOAD QcUnload;
DRIVER_DISPATCH QcDispatchPass;
DRIVER_DISPATCH QcDispatchPnP;
DRIVER_DISPATCH QcDispatchPower;
DRIVER_DISPATCH QcDispatchInternal;
DRIVER_DISPATCH QcDispatchControl;
DRIVER_DISPATCH QcDispatchCleanup;

static ULONG QcModifierForKey(const KEYBOARD_INPUT_DATA* key)
{
    if (key->MakeCode == 0x1D) return QC_MOD_CTRL;
    if (key->MakeCode == 0x38) return QC_MOD_ALT;
    if (key->MakeCode == 0x2A || key->MakeCode == 0x36) return QC_MOD_SHIFT;
    if ((key->Flags & KEY_E0) && (key->MakeCode == 0x5B || key->MakeCode == 0x5C)) return QC_MOD_WIN;
    return 0;
}

static BOOLEAN QcIsConfiguredKey(const KEYBOARD_INPUT_DATA* key)
{
    USHORT requiredFlags = 0;
    if (g_Binding.Flags & QC_KEY_E0) requiredFlags |= KEY_E0;
    if (g_Binding.Flags & QC_KEY_E1) requiredFlags |= KEY_E1;
    return g_Binding.Enabled
        && key->MakeCode == g_Binding.MakeCode
        && (key->Flags & (KEY_E0 | KEY_E1)) == requiredFlags;
}

static VOID QcKeyboardServiceCallback(
    PDEVICE_OBJECT device,
    PKEYBOARD_INPUT_DATA inputStart,
    PKEYBOARD_INPUT_DATA inputEnd,
    PULONG inputConsumed)
{
    PQC_DEVICE_EXTENSION extension = (PQC_DEVICE_EXTENSION)device->DeviceExtension;
    PKEYBOARD_INPUT_DATA current;
    ULONG totalConsumed = 0;

    for (current = inputStart; current < inputEnd; ++current) {
        ULONG modifier;
        BOOLEAN trigger;
        PKEVENT eventObject = NULL;
        KIRQL oldIrql;

        KeAcquireSpinLock(&g_StateLock, &oldIrql);
        modifier = QcModifierForKey(current);
        if (modifier) {
            if (current->Flags & KEY_BREAK) g_Modifiers &= ~modifier;
            else g_Modifiers |= modifier;
        }
        trigger = FALSE;
        if (QcIsConfiguredKey(current)) {
            if (current->Flags & KEY_BREAK) {
                trigger = g_TriggerHeld;
                g_TriggerHeld = FALSE;
            } else if (g_Modifiers == g_Binding.Modifiers) {
                trigger = TRUE;
                if (!g_TriggerHeld) eventObject = g_TriggerEvent;
                g_TriggerHeld = TRUE;
            }
        }
        if (eventObject) KeSetEvent(eventObject, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&g_StateLock, oldIrql);

        if (trigger) {
            ++totalConsumed;
            continue;
        }

        if (extension->UpperConnect.ClassService) {
            ULONG consumed = 0;
            ((PSERVICE_CALLBACK_ROUTINE)extension->UpperConnect.ClassService)(
                extension->UpperConnect.ClassDeviceObject,
                current, current + 1, &consumed);
            totalConsumed += consumed;
            if (consumed == 0) break;
        }
    }
    *inputConsumed = totalConsumed;
}

static VOID QcReplaceEvent(PKEVENT newEvent)
{
    PKEVENT oldEvent;
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_StateLock, &oldIrql);
    oldEvent = g_TriggerEvent;
    g_TriggerEvent = newEvent;
    KeReleaseSpinLock(&g_StateLock, oldIrql);
    if (oldEvent) ObDereferenceObject(oldEvent);
}

NTSTATUS QcDispatchControl(PDEVICE_OBJECT device, PIRP irp)
{
    PQC_DEVICE_EXTENSION extension = (PQC_DEVICE_EXTENSION)device->DeviceExtension;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR information = 0;

    if (extension->Kind == QcDeviceFilter) {
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(extension->LowerDevice, irp);
    }
    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_QC_SET_BINDING
        && stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(QC_DRIVER_BINDING)) {
        KIRQL oldIrql;
        KeAcquireSpinLock(&g_StateLock, &oldIrql);
        g_Binding = *(QC_DRIVER_BINDING*)irp->AssociatedIrp.SystemBuffer;
        g_Modifiers = 0;
        g_TriggerHeld = FALSE;
        KeReleaseSpinLock(&g_StateLock, oldIrql);
        status = STATUS_SUCCESS;
    } else if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_QC_REGISTER_EVENT
        && stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(QC_DRIVER_EVENT)) {
        QC_DRIVER_EVENT* request = (QC_DRIVER_EVENT*)irp->AssociatedIrp.SystemBuffer;
        PKEVENT eventObject = NULL;
        status = ObReferenceObjectByHandle(
            (HANDLE)(ULONG_PTR)request->EventHandle,
            EVENT_MODIFY_STATE, *ExEventObjectType, UserMode,
            (PVOID*)&eventObject, NULL);
        if (NT_SUCCESS(status)) QcReplaceEvent(eventObject);
    }

    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS QcDispatchCleanup(PDEVICE_OBJECT device, PIRP irp)
{
    PQC_DEVICE_EXTENSION extension = (PQC_DEVICE_EXTENSION)device->DeviceExtension;
    if (extension->Kind == QcDeviceFilter) {
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(extension->LowerDevice, irp);
    }
    QcReplaceEvent(NULL);
    {
        KIRQL oldIrql;
        KeAcquireSpinLock(&g_StateLock, &oldIrql);
        g_Binding.Enabled = FALSE;
        g_TriggerHeld = FALSE;
        KeReleaseSpinLock(&g_StateLock, oldIrql);
    }
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS QcDispatchPass(PDEVICE_OBJECT device, PIRP irp)
{
    PQC_DEVICE_EXTENSION extension = (PQC_DEVICE_EXTENSION)device->DeviceExtension;
    if (extension->Kind == QcDeviceControl) {
        irp->IoStatus.Status = STATUS_SUCCESS;
        irp->IoStatus.Information = 0;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(extension->LowerDevice, irp);
}

NTSTATUS QcDispatchInternal(PDEVICE_OBJECT device, PIRP irp)
{
    PQC_DEVICE_EXTENSION extension = (PQC_DEVICE_EXTENSION)device->DeviceExtension;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_KEYBOARD_CONNECT) {
        PCONNECT_DATA connect;
        if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONNECT_DATA)) {
            irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
            IoCompleteRequest(irp, IO_NO_INCREMENT);
            return STATUS_INVALID_PARAMETER;
        }
        if (extension->UpperConnect.ClassService) {
            irp->IoStatus.Status = STATUS_SHARING_VIOLATION;
            IoCompleteRequest(irp, IO_NO_INCREMENT);
            return STATUS_SHARING_VIOLATION;
        }
        connect = (PCONNECT_DATA)stack->Parameters.DeviceIoControl.Type3InputBuffer;
        extension->UpperConnect = *connect;
        connect->ClassDeviceObject = device;
        connect->ClassService = (PVOID)QcKeyboardServiceCallback;
    }
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(extension->LowerDevice, irp);
}

NTSTATUS QcDispatchPower(PDEVICE_OBJECT device, PIRP irp)
{
    PQC_DEVICE_EXTENSION extension = (PQC_DEVICE_EXTENSION)device->DeviceExtension;
    PoStartNextPowerIrp(irp);
    IoSkipCurrentIrpStackLocation(irp);
    return PoCallDriver(extension->LowerDevice, irp);
}

NTSTATUS QcDispatchPnP(PDEVICE_OBJECT device, PIRP irp)
{
    PQC_DEVICE_EXTENSION extension = (PQC_DEVICE_EXTENSION)device->DeviceExtension;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    if (stack->MinorFunction == IRP_MN_REMOVE_DEVICE) {
        NTSTATUS status;
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(extension->LowerDevice, irp);
        IoDetachDevice(extension->LowerDevice);
        IoDeleteDevice(device);
        return status;
    }
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(extension->LowerDevice, irp);
}

NTSTATUS QcAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT physicalDevice)
{
    PDEVICE_OBJECT filterDevice = NULL;
    PQC_DEVICE_EXTENSION extension;
    NTSTATUS status = IoCreateDevice(driver, sizeof(QC_DEVICE_EXTENSION), NULL,
        FILE_DEVICE_KEYBOARD, 0, FALSE, &filterDevice);
    if (!NT_SUCCESS(status)) return status;

    extension = (PQC_DEVICE_EXTENSION)filterDevice->DeviceExtension;
    RtlZeroMemory(extension, sizeof(*extension));
    extension->Kind = QcDeviceFilter;
    extension->LowerDevice = IoAttachDeviceToDeviceStack(filterDevice, physicalDevice);
    if (!extension->LowerDevice) {
        IoDeleteDevice(filterDevice);
        return STATUS_NO_SUCH_DEVICE;
    }
    filterDevice->Flags |= extension->LowerDevice->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE);
    filterDevice->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

VOID QcUnload(PDRIVER_OBJECT driver)
{
    UNICODE_STRING dosName = RTL_CONSTANT_STRING(QC_DOS_DEVICE);
    UNREFERENCED_PARAMETER(driver);
    QcReplaceEvent(NULL);
    IoDeleteSymbolicLink(&dosName);
    if (g_ControlDevice) IoDeleteDevice(g_ControlDevice);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registryPath)
{
    UNICODE_STRING deviceName = RTL_CONSTANT_STRING(QC_CONTROL_DEVICE);
    UNICODE_STRING dosName = RTL_CONSTANT_STRING(QC_DOS_DEVICE);
    PQC_DEVICE_EXTENSION extension;
    NTSTATUS status;
    ULONG index;

    UNREFERENCED_PARAMETER(registryPath);
    KeInitializeSpinLock(&g_StateLock);
    RtlZeroMemory(&g_Binding, sizeof(g_Binding));

    for (index = 0; index <= IRP_MJ_MAXIMUM_FUNCTION; ++index)
        driver->MajorFunction[index] = QcDispatchPass;
    driver->MajorFunction[IRP_MJ_PNP] = QcDispatchPnP;
    driver->MajorFunction[IRP_MJ_POWER] = QcDispatchPower;
    driver->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = QcDispatchInternal;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = QcDispatchControl;
    driver->MajorFunction[IRP_MJ_CLEANUP] = QcDispatchCleanup;
    driver->DriverExtension->AddDevice = QcAddDevice;
    driver->DriverUnload = QcUnload;

    {
        UNICODE_STRING sddl = RTL_CONSTANT_STRING(
            L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)");
        status = IoCreateDeviceSecure(driver, sizeof(QC_DEVICE_EXTENSION),
            &deviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN,
            FALSE, &sddl, &g_QcDeviceClass, &g_ControlDevice);
    }
    if (!NT_SUCCESS(status)) return status;
    extension = (PQC_DEVICE_EXTENSION)g_ControlDevice->DeviceExtension;
    RtlZeroMemory(extension, sizeof(*extension));
    extension->Kind = QcDeviceControl;
    g_ControlDevice->Flags |= DO_BUFFERED_IO;
    g_ControlDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    status = IoCreateSymbolicLink(&dosName, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_ControlDevice);
        g_ControlDevice = NULL;
    }
    return status;
}
