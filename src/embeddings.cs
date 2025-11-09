using System;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace embeddings {
    [DebuggerDisplay("Uiid: {ToGuid()}")]
    [StructLayout(LayoutKind.Explicit, Pack = 1)]
    public unsafe struct Uiid {
        public static readonly Guid Empty;
        [FieldOffset(0)] fixed byte bytes[16];
        public byte[] ToByteArray() {
            byte[] b = new byte[16];
            unsafe {
                for (int i = 0; i < 16; i++)
                    b[i] = bytes[i];
            }
            return b;
        }
        public Guid ToGuid() {
            int a = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
            short b = (short)((bytes[4] << 8) | bytes[5]);
            short c = (short)((bytes[6] << 8) | bytes[7]);
            return new Guid(a, b, c,
                bytes[8],
                bytes[9],
                bytes[10],
                bytes[11],
                bytes[12],
                bytes[13],
                bytes[14],
                bytes[15]);
        }
        public static Uiid FromGuid(Guid g) {
            Uiid u = default;
            byte* x = (byte*)&g;
            byte* d = u.bytes;
            d[0] = x[3]; d[1] = x[2]; d[2] = x[1]; d[3] = x[0];
            d[4] = x[5]; d[5] = x[4];
            d[6] = x[7]; d[7] = x[6];
            d[8] = x[8]; d[9] = x[9]; d[10] = x[10]; d[11] = x[11];
            d[12] = x[12]; d[13] = x[13]; d[14] = x[14]; d[15] = x[15];
            return u;
        }
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct FileHeader {
        public unsafe fixed byte magic[16];
        public UInt32 version;
        public UInt32 size;
        public UInt32 alignment;
        public UInt32 blobSize;
        public byte dtype;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Score {
        public Uiid id;
        public float score;
    }

    public static unsafe class Embeddings {
        private const string DLL = "x64\\embeddings.pyd";

        [DllImport(DLL, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        internal static extern IntPtr fileopen(
            string szPath,
            UInt32 access,
            UInt32 creationDisposition,
            UInt32 blobSize);


        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern int fileappend(
            IntPtr db,
            ref Uiid id,
            IntPtr blob,
            UInt32 blobSize,
            int bFlush /* BOOL */);

        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern void fileclose(
            IntPtr db);

        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern Int32 cosinesearch(
            IntPtr db,
            float* query,
            UInt32 len,
            UInt32 topk,
            [Out] Score[] scores,
            float threshold);

        /* Cursor* __stdcall cursoropen(Embeddings* db); */
        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern IntPtr cursoropen(
            IntPtr db);

        /* void __stdcall cursorclose(Cursor* cur); */
        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern void cursorclose(
            IntPtr cur);

        /* BOOL __stdcall cursorreset(Cursor* cur); */
        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern int cursorreset(
            IntPtr cur);

        /* BOOL __stdcall cursorread(Cursor* cur, DWORD* err); */
        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern int cursorread(
            IntPtr cur,
            out UInt32 err);

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        internal struct Cursor {
            public IntPtr hReadWrite;
            public FileHeader header;
            public UInt64 offset;
            public UInt32 cc;
            public void* buffer;
            public Uiid* id;
            public byte* blob;
            public UInt32 blobSize;
        }

        const uint FILE_READ_DATA = 0x0001;
        const uint FILE_WRITE_DATA = 0x0002;
        const uint FILE_APPEND_DATA = 0x0004;

        const uint FILE_SHARE_READ = 0x00000001;
        const uint FILE_SHARE_WRITE = 0x00000002;

        const uint OPEN_EXISTING = 3;
        const uint OPEN_ALWAYS = 4;
        const uint CREATE_ALWAYS = 2;

        const uint FILE_ATTRIBUTE_NORMAL = 0x00000080;
        const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;

        public static IntPtr Open(
            string pathOrNull,
            string mode,
            uint dim) {
            uint access = 0;
            uint disposition = 0;
            if (string.IsNullOrEmpty(mode) || mode == "r") {
                disposition = OPEN_EXISTING;
                access = FILE_READ_DATA;
            } else if (mode == "a") {
                disposition = OPEN_EXISTING;
                access = FILE_READ_DATA | FILE_APPEND_DATA;
            } else if (mode == "a+") {
                disposition = OPEN_ALWAYS;
                access = FILE_READ_DATA | FILE_APPEND_DATA;
            } else if (mode == "a++") {
                disposition = CREATE_ALWAYS;
                access = FILE_READ_DATA | FILE_APPEND_DATA;
            } else {
                throw new ArgumentException("Unsupported mode. Use \"r\", \"a\", \"a+\", or \"a++\".", nameof(mode));
            }
            return fileopen(
                pathOrNull,
                access,
                disposition,
                dim * 4u);
        }

        public static void Close(IntPtr db) {
            fileclose(db);
        }

        public static bool Append(
            IntPtr db,
            ref Uiid id,
            void* blobPtr,
            uint blobSizeBytes,
            bool flush) {
            int ok = fileappend(
                db,
                ref id,
                (IntPtr)blobPtr,
                blobSizeBytes,
                flush ? 1 : 0);
            return ok != 0;
        }

        public static int Search(
            IntPtr db,
            float* queryPtr,
            uint len,
            uint topk,
            float threshold,
            out Score[] results) {
            Score[] scores = new Score[topk];
            int count = cosinesearch(
                db,
                queryPtr,
                len,
                topk,
                scores,
                threshold);
            if (count < 0) {
                results = new Score[0];
                return count;
            }
            results = scores;
            return count;
        }

        /* Cursor API: zero-copy sequential scan
         *
         * Usage:
         *   IntPtr cur = Embeddings.CursorOpen(db);
         *   while (Embeddings.CursorRead(cur, out recView)) { ... }
         *   Embeddings.CursorClose(cur);
         *
         * After each successful CursorRead:
         *   recView.Id points to uiid inside unmanaged buffer
         *   recView.Blob points to blob b inside unmanaged buffer
         *   recView.BlobSize is the blob length
         *
         * That buffer stays valid until the next CursorRead or CursorClose.
         */

        public static IntPtr CursorOpen(IntPtr db) {
            return cursoropen(db);
        }

        public static void CursorClose(IntPtr cur) {
            if (cur != IntPtr.Zero) {
                cursorclose(cur);
            }
        }

        public static bool CursorReset(IntPtr cur) {
            return cursorreset(cur) != 0;
        }

        // public static bool CursorRead(
        //     IntPtr cur,
        //     out uint errorCode /* 0 or ERROR_HANDLE_EOF etc. */) {
        //     UInt32 err;
        //     int ok = Native.cursorread(cur, out err);
        // 
        //     errorCode = err;
        //     if (ok == 0) {
        //         // FALSE -> either EOF or real error
        //         view = default(RecordView);
        //         return false;
        //     }
        // 
        //     // cursorread just advanced internal buffer.
        //     // Now we need to reinterpret the native Cursor struct
        //     // so we can extract id/blob/blobSize addresses with no copy.
        // 
        //     Native.Cursor cm = (Native.Cursor)Marshal.PtrToStructure(
        //         cur,
        //         typeof(Native.Cursor));
        // 
        //     view = new RecordView {
        //         Id = (Uiid*)cm.id,
        //         Blob = (byte*)cm.blob,
        //         BlobSize = cm.blobSize
        //     };
        // 
        //     return true;
        // }
        // 
        // /* Helpers */
        // 
        // public static Uiid GuidToUiid(Guid g) {
        //     // This copies a Guid into uiid layout.
        //     byte[] b = g.ToByteArray();
        //     Uiid u = new Uiid();
        //     unsafe {
        //         for (int i = 0; i < 16; i++)
        //             u.b[i] = b[i];
        //     }
        //     return u;
        // }
        // 
        // public static Guid UiidToGuid(ref Uiid u) {
        //     byte[] b = new byte[16];
        //     unsafe {
        //         for (int i = 0; i < 16; i++)
        //             b[i] = u.b[i];
        //     }
        //     return new Guid(b);
        // }

        /* Memset helper */
        [System.Runtime.InteropServices.DllImport("kernel32.dll", EntryPoint = "RtlZeroMemory", SetLastError = false)]
        private static extern void ZeroMemory(IntPtr dest, UIntPtr size);
    }
}
