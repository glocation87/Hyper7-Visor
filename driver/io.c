#include "hv7.h"

static PDEVICE_OBJECT g_dev = NULL;

static NTSTATUS complete(PIRP irp, NTSTATUS st, ULONG_PTR bytes)
{
    irp->IoStatus.Status = st;
    irp->IoStatus.Information = bytes;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return st;
}

static NTSTATUS on_create_close(PDEVICE_OBJECT dev, PIRP irp)
{
    UNREFERENCED_PARAMETER(dev);
    return complete(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS on_ioctl(PDEVICE_OBJECT dev, PIRP irp)
{
    UNREFERENCED_PARAMETER(dev);

    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(irp);
    ULONG code = sp->Parameters.DeviceIoControl.IoControlCode;
    ULONG out_len = sp->Parameters.DeviceIoControl.OutputBufferLength;
    void *buf = irp->AssociatedIrp.SystemBuffer;

    switch (code) {
    case IOCTL_H7_PING: {
        if (out_len < sizeof(ULONG64))
            return complete(irp, STATUS_BUFFER_TOO_SMALL, 0);
        *(ULONG64 *)buf = h7_hypercall(HC_PING, 0, 0, 0);
        return complete(irp, STATUS_SUCCESS, sizeof(ULONG64));
    }

    case IOCTL_H7_STATS: {
        if (out_len < sizeof(h7_stats_out))
            return complete(irp, STATUS_BUFFER_TOO_SMALL, 0);
        h7_stats *s = h7_get_stats();
        h7_stats_out *o = buf;
        o->total       = s->total;
        o->cpuid       = s->cpuid;
        o->msr         = s->msr;
        o->rdtsc       = s->rdtsc;
        o->rdtscp      = s->rdtscp;
        o->vmmcall     = s->vmmcall;
        o->cr3_write   = s->cr3_write;
        o->npf         = s->npf;
        o->injected_ud = s->injected_ud;
        return complete(irp, STATUS_SUCCESS, sizeof(h7_stats_out));
    }

    case IOCTL_H7_CR3_WATCH: {
        ULONG in_len = sp->Parameters.DeviceIoControl.InputBufferLength;
        if (in_len < sizeof(ULONG))
            return complete(irp, STATUS_BUFFER_TOO_SMALL, 0);
        h7_hypercall(HC_CR3_WATCH, *(ULONG *)buf, 0, 0);
        return complete(irp, STATUS_SUCCESS, 0);
    }

    case IOCTL_H7_CR3_SAMPLE: {
        if (out_len < sizeof(ULONG64))
            return complete(irp, STATUS_BUFFER_TOO_SMALL, 0);
        ULONG64 cr3 = 0;
        h7_hypercall(HC_CR3_SAMPLE, (ULONG64)&cr3, 0, 0);
        *(ULONG64 *)buf = cr3;
        return complete(irp, STATUS_SUCCESS, sizeof(ULONG64));
    }

    case IOCTL_H7_UNLOAD:
        h7_shutdown();
        return complete(irp, STATUS_SUCCESS, 0);

    default:
        return complete(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

NTSTATUS h7_io_init(PDRIVER_OBJECT drv)
{
    UNICODE_STRING dev_name = RTL_CONSTANT_STRING(H7_DEVICE_NAME_W);
    UNICODE_STRING link     = RTL_CONSTANT_STRING(H7_SYMLINK_W);

    NTSTATUS st = IoCreateDevice(drv, 0, &dev_name, FILE_DEVICE_UNKNOWN,
                                 FILE_DEVICE_SECURE_OPEN, FALSE, &g_dev);
    if (!NT_SUCCESS(st)) return st;

    st = IoCreateSymbolicLink(&link, &dev_name);
    if (!NT_SUCCESS(st)) {
        IoDeleteDevice(g_dev);
        g_dev = NULL;
        return st;
    }

    drv->MajorFunction[IRP_MJ_CREATE] = on_create_close;
    drv->MajorFunction[IRP_MJ_CLOSE]  = on_create_close;
    drv->MajorFunction[IRP_MJ_DEVICE_CONTROL] = on_ioctl;
    return STATUS_SUCCESS;
}

void h7_io_cleanup(PDRIVER_OBJECT drv)
{
    UNREFERENCED_PARAMETER(drv);
    UNICODE_STRING link = RTL_CONSTANT_STRING(H7_SYMLINK_W);
    IoDeleteSymbolicLink(&link);
    if (g_dev) {
        IoDeleteDevice(g_dev);
        g_dev = NULL;
    }
}
