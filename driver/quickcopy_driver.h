#ifndef QUICKCOPY_DRIVER_H
#define QUICKCOPY_DRIVER_H

#define QC_DRIVER_DEVICE_PATH L"\\\\.\\QuickCopyKeyboardFilter"

#define IOCTL_QC_SET_BINDING CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_WRITE_DATA)
#define IOCTL_QC_REGISTER_EVENT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)

#define QC_MOD_CTRL  0x01
#define QC_MOD_ALT   0x02
#define QC_MOD_SHIFT 0x04
#define QC_MOD_WIN   0x08

#define QC_KEY_E0 0x01
#define QC_KEY_E1 0x02

typedef struct _QC_DRIVER_BINDING {
    unsigned short MakeCode;
    unsigned short Flags;
    unsigned long Modifiers;
    unsigned long Enabled;
} QC_DRIVER_BINDING;

typedef struct _QC_DRIVER_EVENT {
    unsigned long long EventHandle;
} QC_DRIVER_EVENT;

#endif
