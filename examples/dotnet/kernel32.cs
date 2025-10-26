#pragma warning disable CS8981

using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class kernel32 {
    static kernel32() {
        if (IntPtr.Size != sizeof(long)) {
            throw new PlatformNotSupportedException();
        }
    }

    [DllImport("kernel32.dll", CallingConvention = CallingConvention.StdCall, SetLastError = true)]
    public static extern IntPtr LoadLibraryW(
        [MarshalAs(UnmanagedType.LPWStr)] string lpLibFileName);

    [DllImport("kernel32.dll", CallingConvention = CallingConvention.StdCall, SetLastError = true)]
    public static extern bool FreeLibrary(IntPtr hModule);

    public static bool LoadLibraryW(string lpLibFileName, out IntPtr hModule) {
        hModule = LoadLibraryW(lpLibFileName);
        if (hModule != IntPtr.Zero) {
            return true;
        }
        return false;
    }

    [DllImport("kernel32.dll", CallingConvention = CallingConvention.StdCall, SetLastError = true)]
    public static extern IntPtr GetProcAddress(
        IntPtr hModule,
        [MarshalAs(UnmanagedType.LPStr)] String lpProcName);

    public static bool GetProcAddress(IntPtr hModule, string lpProcName, out IntPtr lpProcAddress) {
        lpProcAddress = GetProcAddress(hModule, lpProcName);
        if (lpProcAddress != IntPtr.Zero) {
            return true;
        }
        return false;
    }

    public static bool GetProcAddress<T>(IntPtr hModule, string lpProcName, out T lpProc)
        where T: Delegate {
        lpProc = null;
        var lpProcAddress = GetProcAddress(hModule, lpProcName);
        if (lpProcAddress != IntPtr.Zero) {
            lpProc = Marshal.GetDelegateForFunctionPointer<T>(lpProcAddress);
            return true;
        }
        return false;
    }

    [DllImport("kernel32.dll", CallingConvention = CallingConvention.StdCall, SetLastError = true)]
    public unsafe static extern uint GetModuleFileNameW(
        IntPtr hModule,
        char* lpFilename,
        uint nSize);

    public unsafe static string GetModuleFileName(IntPtr hModule) {
        char* lpFilename = stackalloc char[1024];
        var sz = GetModuleFileNameW(hModule, lpFilename, 1024);
        if (sz == 0) {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        return new string(lpFilename, 0, (int)sz);
    }

    [Flags]
    public enum AllocationTypes : uint {
        Commit = 0x1000,
        Reserve = 0x2000,
        Reset = 0x80000,
        LargePages = 0x20000000,
        Physical = 0x400000,
        TopDown = 0x100000,
        WriteWatch = 0x200000
    }

    [Flags]
    public enum MemoryProtections : uint {
        Execute = 0x10,
        ExecuteRead = 0x20,
        ExecuteReadWrite = 0x40,
        ExecuteWriteCopy = 0x80,
        NoAccess = 0x01,
        ReadOnly = 0x02,
        ReadWrite = 0x04,
        WriteCopy = 0x08,
        GuartModifierflag = 0x100,
        NoCacheModifierflag = 0x200,
        WriteCombineModifierflag = 0x400
    }

    [Flags]
    public enum FreeTypes : uint {
        Decommit = 0x4000,
        Release = 0x8000
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr VirtualAlloc(
            IntPtr lpAddress,
            IntPtr dwSize,
            AllocationTypes flAllocationType,
            MemoryProtections flProtect);

    [DllImport("kernel32")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool VirtualFree(
        IntPtr lpAddress,
        IntPtr dwSize,
        FreeTypes flFreeType);

    public static IntPtr VirtualAllocExecuteReadWrite(byte[] byteCode) {
        if (byteCode is null) {
            throw new ArgumentNullException(nameof(byteCode));
        }
        var lpAddress = VirtualAlloc(IntPtr.Zero, new IntPtr(byteCode.Length),
            AllocationTypes.Commit | AllocationTypes.Reserve, MemoryProtections.ExecuteReadWrite);
        if (lpAddress == IntPtr.Zero) {
            throw new OutOfMemoryException();
        }
        Marshal.Copy(byteCode,
            0,
            lpAddress,
            byteCode.Length
        );
        return lpAddress;
    }

    [DllImport("kernel32.dll", EntryPoint = "GetTickCount64", SetLastError = false)]
    public static extern ulong millis();

    [DllImport("kernel32.dll", EntryPoint = "RtlCopyMemory", SetLastError = false)]
    public static extern unsafe void CopyMemory(void* dst, void* src, ulong size);

    [DllImport("kernel32.dll", EntryPoint = "RtlFillMemory", SetLastError = false)]
    public static extern unsafe void FillMemory(void* dst, ulong size, byte fill);

    public const int LMEM_FIXED          = 0x0000;
    public const int LMEM_ZEROINIT       = 0x0040;
    public const int LPTR                = 0x0040;
    public const int LMEM_MOVEABLE       = 0x0002;

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern unsafe void* LocalAlloc(int uFlags, ulong size);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern unsafe void* LocalReAlloc(void* handle, ulong size, int uFlags);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern unsafe void LocalFree(void* handle);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern unsafe void LocalFree(IntPtr handle);

    [DllImport("kernel32.dll", EntryPoint = "RtlZeroMemory")]
    public static extern unsafe void ZeroMemory(void* address, ulong size);

    [DllImport("kernel32.dll", EntryPoint = "RtlZeroMemory")]
    public static extern unsafe void ZeroMemory(IntPtr address, ulong size);

    public static IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern int GetFileSize(IntPtr hFile, out int dwHighSize);

    [DllImport("kernel32.dll", EntryPoint = "SetFilePointer", SetLastError = true)]
    public unsafe static extern int SetFilePointerWin32(IntPtr hFile, int lo, int* hi, int origin);

    public const uint GENERIC_READ = 0x80000000;
    public const uint GENERIC_WRITE = 0x40000000;
    public const uint GENERIC_EXECUTE = 0x20000000;
    public const uint GENERIC_ALL = 0x10000000;

    public const uint FILE_ATTRIBUTE_READONLY = 0x00000001;
    public const uint FILE_ATTRIBUTE_HIDDEN = 0x00000002;
    public const uint FILE_ATTRIBUTE_SYSTEM = 0x00000004;
    public const uint FILE_ATTRIBUTE_DIRECTORY = 0x00000010;
    public const uint FILE_ATTRIBUTE_ARCHIVE = 0x00000020;
    public const uint FILE_ATTRIBUTE_DEVICE = 0x00000040;
    public const uint FILE_ATTRIBUTE_NORMAL = 0x00000080;
    public const uint FILE_ATTRIBUTE_TEMPORARY = 0x00000100;

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr handle);

    [Flags]
    public enum ShareMode : uint {
        None = 0x00000000,
        Read = 0x00000001,
        Write = 0x00000002,
        Delete = 0x00000004
    }

    public enum CreationDisposition : uint {
        New = 1,
        CreateAlways = 2,
        OpenExisting = 3,
        OpenAlways = 4,
        TruncateExisting = 5
    }

    [DllImport("kernel32.dll", BestFitMapping = false, CharSet = CharSet.Auto, SetLastError = true)]
    public static extern IntPtr CreateFile(
        string lpFileName,
        uint dwDesiredAccess,
        ShareMode dwShareMode,
        IntPtr lpSecurityAttributes,
        CreationDisposition dwCreationDisposition,
        uint dwFlagsAndAttributes,
        IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    public unsafe static extern int ReadFile(
        IntPtr hFile,
        void* lpBuffer,
        uint nNumberOfBytesToRead,
        out uint lpNumberOfBytesRead,
        IntPtr lpOverlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    public unsafe static extern int WriteFile(
        IntPtr hFile,
        void* lpBuffer,
        uint nNumberOfBytesToWrite,
        out uint lpNumberOfBytesWritten,
        IntPtr mustBeZero);

    public const uint SECTION_QUERY = 0x0001;
    public const uint SECTION_MAP_WRITE = 0x0002;
    public const uint SECTION_MAP_READ = 0x0004;
    public const uint SECTION_MAP_EXECUTE = 0x0008;
    public const uint SECTION_EXTEND_SIZE = 0x0010;
    public const uint SECTION_MAP_EXECUTE_EXPLICIT = 0x0020; // not included in SECTION_ALL_ACCESS

    public const uint FILE_MAP_WRITE = SECTION_MAP_WRITE;
    public const uint FILE_MAP_READ = SECTION_MAP_READ;
    public const uint FILE_MAP_EXECUTE = SECTION_MAP_EXECUTE_EXPLICIT;  // not included in FILE_MAP_ALL_ACCESS
    public const uint FILE_MAP_COPY = 0x00000001;
    public const uint FILE_MAP_RESERVE = 0x80000000;
    public const uint FILE_MAP_TARGETS_INVALID = 0x40000000;
    public const uint FILE_MAP_LARGE_PAGES = 0x20000000;

    /// <summary>
    /// Allows views to be mapped for read-only, copy-on-write, or execute access.
    /// The file handle specified by the hFile parameter must be created with the GENERIC_READ and GENERIC_EXECUTE access rights.
    /// Windows Server 2003 and Windows XP:  This value is not available until Windows XP with SP2 and Windows Server 2003 with SP1.
    /// </summary>
    public const uint PAGE_EXECUTE_READ = 0x20;

    /// <summary>
    /// Allows views to be mapped for read-only, copy-on-write, read/write, or execute access.
    /// The file handle that the hFile parameter specifies must be created with the GENERIC_READ, GENERIC_WRITE, and GENERIC_EXECUTE access rights.
    /// Windows Server 2003 and Windows XP:  This value is not available until Windows XP with SP2 and Windows Server 2003 with SP1.
    /// </summary>
    public const uint PAGE_EXECUTE_READWRITE = 0x40;

    /// <summary>
    /// Allows views to be mapped for read-only, copy-on-write, or execute access.This value is equivalent to PAGE_EXECUTE_READ.
    /// The file handle that the hFile parameter specifies must be created with the GENERIC_READ and GENERIC_EXECUTE access rights.
    /// Windows Vista:  This value is not available until Windows Vista with SP1.
    /// Windows Server 2003 and Windows XP:  This value is not supported.
    /// </summary>
    public const uint PAGE_EXECUTE_WRITECOPY = 0x80;

    /// <summary>
    /// Allows views to be mapped for read-only or copy-on-write access. An attempt to write to a specific region results in an access violation.
    /// The file handle that the hFile parameter specifies must be created with the GENERIC_READ access right.
    /// </summary>
    public const uint PAGE_READONLY = 0x02;

    /// <summary>
    /// Allows views to be mapped for read-only, copy-on-write, or read/write access.
    /// The file handle that the hFile parameter specifies must be created with the GENERIC_READ and GENERIC_WRITE access rights.
    /// </summary>
    public const uint PAGE_READWRITE = 0x04;

    /// <summary>
    /// Allows views to be mapped for read-only or copy-on-write access. This value is equivalent to PAGE_READONLY.
    /// The file handle that the hFile parameter specifies must be created with the GENERIC_READ access right.
    /// </summary>
    public const uint PAGE_WRITECOPY = 0x08;

    [DllImport("kernel32.dll", BestFitMapping = false, SetLastError = true)]
    public unsafe static extern IntPtr CreateFileMappingW(
        IntPtr hFile,
        IntPtr lpSecurityAttributes,
        uint flProtect,
        uint dwMaximumSizeHigh,
        uint dwMaximumSizeLow,
        IntPtr lpName);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static unsafe extern bool UnmapViewOfFile(UIntPtr lpBaseAddress);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static unsafe extern UIntPtr MapViewOfFile(
        IntPtr hFileMappingObject,
        uint dwDesiredAccess,
        uint dwFileOffsetHigh,
        uint dwFileOffsetLow,
        ulong dwNumberOfBytesToMap);
}