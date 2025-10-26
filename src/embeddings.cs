/* Embeddings.cs
 *
 * .NET Framework 4.8 static wrapper over embeddings.dll
 * Zero extra copies where possible.
 *
 * IMPORTANT MEMORY RULES:
 * - You MUST call Embeddings.Close(db) when done (it closes the OS handle and zeroes the struct).
 * - A Cursor is unmanaged memory owned by the native DLL. After CursorClose(cur) it's invalid.
 * - Record.id / Record.blob pointers are only valid until the NEXT CursorRead or CursorClose.
 *
 * Build:
 *   csc /unsafe /define:TRACE /target:library Embeddings.cs
 */

using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security;

namespace embeddings {
    /* Mirror of uiid (16 bytes) */
    [DebuggerDisplay("Uiid: {ToGuid()}")]
    [StructLayout(LayoutKind.Explicit, Pack = 1, Size = 16)]
    public unsafe struct Uiid {
        [FieldOffset(0)] public fixed byte bytes[16];
        // Sequential big-endian fields (RFC 4122 order)
        [FieldOffset(0)] public byte b0;
        [FieldOffset(1)] public byte b1;
        [FieldOffset(2)] public byte b2;
        [FieldOffset(3)] public byte b3;
        [FieldOffset(4)] public byte b4;
        [FieldOffset(5)] public byte b5;
        [FieldOffset(6)] public byte b6;
        [FieldOffset(7)] public byte b7;
        [FieldOffset(8)] public byte b8;
        [FieldOffset(9)] public byte b9;
        [FieldOffset(10)] public byte b10;
        [FieldOffset(11)] public byte b11;
        [FieldOffset(12)] public byte b12;
        [FieldOffset(13)] public byte b13;
        [FieldOffset(14)] public byte b14;
        [FieldOffset(15)] public byte b15;
        public Guid ToGuid() {
            // RFC 4122 big-endian interpretation
            int a = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
            short b = (short)((b4 << 8) | b5);
            short c = (short)((b6 << 8) | b7);
            // Last 8 bytes are already correct
            return new Guid(a, b, c, b8, b9, b10, b11, b12, b13, b14, b15);
        }
    }

        /* Mirror of FileHeader in case caller wants metadata.
         * NOTE: We keep the same packing (1).
         */
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct FileHeader {
        // char magic[0x10];
        // We'll marshal as 16 separate bytes.
        // Using fixed byte[16] would require unsafe; we can do that.
        public unsafe fixed byte magic[16];

        public UInt32 version;
        public UInt32 size;
        public UInt32 alignment;
        public UInt32 blobSize;
        public byte dtype;
    }

    /* Mirror of SYSTEM_INFO subset we actually need.
     * We'll just preserve layout so Embeddings struct size matches native.
     * Easiest is: don't expose SYSTEM_INFO fields at all in managed struct.
     * We'll treat Embeddings as opaque and only pass it by ref.
     *
     * NOTE:
     * Embeddings { HANDLE hWrite; wchar_t wszPath[PATH]; FileHeader header; SYSTEM_INFO os; }
     * We'll model this as a fixed-size struct with a big char buffer. We MUST match size+layout.
     *
     * For .NET P/Invoke, the most robust zero-copy way for this pattern:
     *   - allocate Embeddings in unmanaged with Marshal.AllocHGlobal(sizeof(Embeddings))
     *   OR
     *   - keep Embeddings as a blittable struct and pin it.
     *
     * We want "no copies" and allow caller to hold it.
     * We'll choose: allocate Embeddings once via Alloc() (Marshal.AllocHGlobal),
     * pass IntPtr to native everywhere. That avoids layout drift worries.
     *
     * So: we will NOT try to mirror native Embeddings exactly in C#.
     * Instead we treat Embeddings* as IntPtr.
     *
     * Same for Cursor*: we'll use IntPtr.
     *
     * That keeps us safe if you tweak struct internals.
     */

    /* Score { uiid id; float score; } */
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Score {
        public Uiid id;
        public float score;
    }

    /* A "view" over the current cursor row with raw pointers.
     * No copies.
     */
    public unsafe struct RecordView {
        public Uiid* Id;        /* points to 16-byte uiid in native cursor buffer */
        public byte* Blob;      /* points to blob bytes in native cursor buffer */
        public uint BlobSize;   /* blob length in bytes */

        public Guid ToGuid() {
            // Guid(byte[]) copies; caller can decide.
            byte[] tmp = new byte[16];
            for (int i = 0; i < 16; i++) tmp[i] = Id->bytes[i];
            return new Guid(tmp);
        }
    }

    [SuppressUnmanagedCodeSecurity]
    internal static unsafe class Native {
        private const string DLL = "D:\\SRC\\embeddings\\embeddings\\embeddings.pyd";

        /* BOOL __stdcall File_open(
         *      Embeddings* db,
         *      const wchar_t* szPath,
         *      DWORD access,
         *      DWORD dwCreationDisposition,
         *      uint32_t dwBlobSize);
         */
        [DllImport(DLL, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        internal static extern int File_open(
            IntPtr db,
            string szPath,
            UInt32 access,
            UInt32 creationDisposition,
            UInt32 blobSize);

        /* BOOL __stdcall File_append(
         *      Embeddings* db,
         *      uiid id,
         *      const void* blob,
         *      DWORD blobSize,
         *      BOOL bFlush);
         */
        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern int File_append(
            IntPtr db,
            ref Uiid id,
            IntPtr blob,
            UInt32 blobSize,
            int bFlush /* BOOL */);

        /* BOOL __stdcall File_scan(
         *      Embeddings* db,
         *      BOOL (__stdcall *callback)(const uiid* id, const void* blob, DWORD blobSize, void* userData),
         *      void* userData);
         *
         * We'll expose Cursor_* instead of direct File_scan, since that's the zero-copy way.
         * But we keep this P/Invoke in case you want it.
         */
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        internal delegate int ScanCallback(
            Uiid* id,
            byte* blob,
            UInt32 blobSize,
            IntPtr userData);

        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern int File_scan(
            IntPtr db,
            ScanCallback callback,
            IntPtr userData);

        /* void __stdcall File_close(Embeddings* db); */
        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern void File_close(
            IntPtr db);

        /* int32_t __stdcall File_search(
         *      Embeddings* db,
         *      const float* query,
         *      DWORD len,
         *      DWORD topk,
         *      Score* scores,
         *      float min);
         */
        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern Int32 File_search(
            IntPtr db,
            float* query,
            UInt32 len,
            UInt32 topk,
            [Out] Score[] scores,
            float threshold);

        /* Cursor* __stdcall Cursor_open(Embeddings* db); */
        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern IntPtr Cursor_open(
            IntPtr db);

        /* void __stdcall Cursor_close(Cursor* cur); */
        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern void Cursor_close(
            IntPtr cur);

        /* BOOL __stdcall Cursor_reset(Cursor* cur); */
        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern int Cursor_reset(
            IntPtr cur);

        /* BOOL __stdcall Cursor_read(Cursor* cur, DWORD* err); */
        [DllImport(DLL, CallingConvention = CallingConvention.StdCall)]
        internal static extern int Cursor_read(
            IntPtr cur,
            out UInt32 err);

        /* We ALSO need to read these cursor fields after each Cursor_read:
         *
         * typedef struct Cursor {
         *   HANDLE hRead;
         *   Embeddings* db;
         *   uint64_t cc;
         *   void* buffer;
         *   uiid* id;
         *   uint8_t* blob;
         *   uint32_t blobSize;
         * } Cursor;
         *
         * We'll mirror JUST this layout to inspect it from managed.
         * This is stable as long as you don't reorder those fields.  (We assume same packing as native, default.)
         */
        [StructLayout(LayoutKind.Sequential)]
        internal struct CursorManaged {
            public IntPtr hRead;
            public IntPtr db;
            public UInt64 cc;
            public IntPtr buffer;
            public IntPtr id;
            public IntPtr blob;
            public UInt32 blobSize;
        }
    }

    /* Public static wrapper */
    public static unsafe class Embeddings {
        /* We treat Embeddings* as IntPtr.
         * Allocate native-sized buffer ourselves and hand its pointer to File_open.
         *
         * The native side expects 'db' to be zeroed first (it calls memset(db,0,...)). :contentReference[oaicite:7]{index=7}
         *
         * We'll just AllocHGlobal(sizeGuess) and zero it.
         *
         * We don't know sizeof(Embeddings) at compile time from C#.
         * BUT: worst case, we can over-allocate, since native only writes inside sizeof(*db).
         * That guess must match the native struct exactly though.
         *
         * Let's compute it manually from header:
         *
         * typedef struct Embeddings {
         *   HANDLE hWrite;              // 8 bytes on x64
         *   wchar_t wszPath[PATH];      // PATH = 1024 wchar_t = 2048 bytes
         *   FileHeader header;          // ~ (16 + 4+4+4+4+1) = 33 bytes => padded to 40-ish
         *   SYSTEM_INFO os;             // ~48 bytes on Win64
         * } Embeddings;
         *
         * We're ~8 + 2048 + 40 + 48 ~= 2144 bytes. We'll allocate, say, 4096 bytes just to be safe.
         *
         * That's fine because:
         *  - We only ever pass the same IntPtr back to native.
         *  - Native treats it as Embeddings*, not reading beyond real struct fields.
         *
         * So db is an opaque handle for us.
         */

        private const int DB_BYTES = 4096;

        public static IntPtr AllocDb() {
            IntPtr db = Marshal.AllocHGlobal(DB_BYTES);
            ZeroMemory(db, (UIntPtr)DB_BYTES);
            return db;
        }

        public static void FreeDb(IntPtr db) {
            if (db != IntPtr.Zero) {
                Marshal.FreeHGlobal(db);
            }
        }

        /* Open:
         * dim = embedding dimension
         * blobSize = dim * sizeof(float)
         *
         * You must give the same dim every time you reopen a file,
         * or File_open will reject blobSize mismatch. :contentReference[oaicite:8]{index=8}
         *
         * access/disposition are straight Win32:
         *   access e.g. GENERIC_READ | FILE_APPEND_DATA
         *   disposition e.g. OPEN_ALWAYS
         */
        public static bool Open(
            IntPtr db,
            string pathOrNull,
            uint access,
            uint creationDisposition,
            uint dim /* number of floats per vector */) {
            // native wants blobSize in bytes
            uint blobBytes = dim * 4u;
            int ok = Native.File_open(db, pathOrNull, access, creationDisposition, blobBytes);
            return ok != 0;
        }

        public static void Close(IntPtr db) {
            Native.File_close(db);
        }

        /* Append:
         * id: 16 bytes (we'll pass by ref, no copy to new buffer)
         * blob: pointer to caller's float[] / byte[] pinned
         * bFlush: true => FlushFileBuffers. :contentReference[oaicite:9]{index=9}
         */
        public static bool Append(
            IntPtr db,
            ref Uiid id,
            void* blobPtr,
            uint blobSizeBytes,
            bool flush) {
            int ok = Native.File_append(
                db,
                ref id,
                (IntPtr)blobPtr,
                blobSizeBytes,
                flush ? 1 : 0);
            return ok != 0;
        }

        /* Search (cosine top-k):
         *
         * queryPtr -> float[len]
         * len      -> count of floats in query
         * topk     -> max results wanted
         * threshold-> min cosine (native arg: "min") :contentReference[oaicite:10]{index=10}
         *
         * returns: number of hits actually written (<= topk)
         * scores[0..count-1] sorted best-first.
         *
         * NOTE: this one DOES allocate a managed Score[] for you to read.
         * The native code fills that array directly (blittable struct).
         */
        public static int Search(
            IntPtr db,
            float* queryPtr,
            uint len,
            uint topk,
            float threshold,
            out Score[] results /* sized to topk */) {
            Score[] scores = new Score[topk];
            int count = Native.File_search(
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

            // We could trim, but that copies. You said avoid copies.
            // So: return the whole scores[] and a count.
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
         *   recView.Blob points to blob bytes inside unmanaged buffer
         *   recView.BlobSize is the blob length
         *
         * That buffer stays valid until the next CursorRead or CursorClose.
         */

        public static IntPtr CursorOpen(IntPtr db) {
            return Native.Cursor_open(db);
        }

        public static void CursorClose(IntPtr cur) {
            if (cur != IntPtr.Zero) {
                Native.Cursor_close(cur);
            }
        }

        public static bool CursorReset(IntPtr cur) {
            return Native.Cursor_reset(cur) != 0;
        }

        public static bool CursorRead(
            IntPtr cur,
            out RecordView view,
            out uint errorCode /* 0 or ERROR_HANDLE_EOF etc. */) {
            UInt32 err;
            int ok = Native.Cursor_read(cur, out err);

            errorCode = err;
            if (ok == 0) {
                // FALSE -> either EOF or real error
                view = default(RecordView);
                return false;
            }

            // Cursor_read just advanced internal buffer.
            // Now we need to reinterpret the native Cursor struct
            // so we can extract id/blob/blobSize addresses with no copy.

            Native.CursorManaged cm = (Native.CursorManaged)Marshal.PtrToStructure(
                cur,
                typeof(Native.CursorManaged));

            view = new RecordView {
                Id = (Uiid*)cm.id,
                Blob = (byte*)cm.blob,
                BlobSize = cm.blobSize
            };

            return true;
        }

        /* Helpers */

        public static Uiid GuidToUiid(Guid g) {
            // This copies a Guid into uiid layout.
            byte[] b = g.ToByteArray();
            Uiid u = new Uiid();
            unsafe {
                for (int i = 0; i < 16; i++)
                    u.bytes[i] = b[i];
            }
            return u;
        }

        public static Guid UiidToGuid(ref Uiid u) {
            byte[] b = new byte[16];
            unsafe {
                for (int i = 0; i < 16; i++)
                    b[i] = u.bytes[i];
            }
            return new Guid(b);
        }

        /* Memset helper */
        [System.Runtime.InteropServices.DllImport("kernel32.dll", EntryPoint = "RtlZeroMemory", SetLastError = false)]
        private static extern void ZeroMemory(IntPtr dest, UIntPtr size);
    }
}
