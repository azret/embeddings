#define EMBEDDINGS_C

#if !defined(_WIN64)
#error "Only 64-bit builds are supported."
#endif

#include "embeddings.h"
#include <assert.h>
#include <time.h>
#include <math.h>

#define VERSION 1

#define __alignup(x,a)  (((x) + ((a) - 1)) & ~((a) - 1))

#if !defined(DEBUGGING)
#define _dbglog
#endif

#if !defined(_dbglog)
#define _dbglog(fmt, ...) \
    do { \
        fprintf(stderr, "[%s:%d] " fmt "", __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
#define _dbglog(fmt, ...)                                                       \
    do {                                                                        \
        time_t _t = time(NULL);                                                 \
        struct tm _tm;                                                          \
        localtime_s(&_tm, &_t);                                                 \
        char _buf[32];                                                          \
        strftime(_buf, sizeof(_buf), "%H:%M:%S", &_tm);                \
        fprintf(stderr, "[%s] [%s:%d] " fmt "", _buf, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
#endif

#define MAXHEAD 4096
#define MAXBLOB 65536

static inline uint32_t __bytwo(uint32_t x)
{
    if (x == 0) return 1;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x++;
    return x;
}

EMBEDDINGS_API BOOL EMBEDDINGS_CALL File_open(Embeddings* db, const wchar_t* pwszpath, DWORD access, DWORD dwCreationDisposition, uint32_t dwBlobSize)
{
    _dbglog("File_open(path='%ls' blob=%u access=0x%08X, disposition=0x%08X);\n", pwszpath, dwBlobSize, access, dwCreationDisposition);
    memset(db, 0, sizeof(*db));
    DWORD flags;
    assert(PATH >= MAX_PATH);
    if (!pwszpath) {
        wchar_t userTempFolder[PATH];
        GetTempPathW(PATH, userTempFolder);
        if (!GetTempFileNameW(userTempFolder, L"embeddings", 0, db->wszPath)) {
            fprintf(stderr, "Failed to create a temporary file name: %lu\n", GetLastError());
            return FALSE;
        }
        flags = FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_SEQUENTIAL_SCAN;
		dwCreationDisposition = CREATE_ALWAYS;
        access = FILE_READ_DATA | FILE_APPEND_DATA | FILE_WRITE_DATA;
		pwszpath = db->wszPath;
    } else {
        if (!GetFullPathNameW(pwszpath, PATH, db->wszPath, NULL)) {
            fprintf(stderr, "GetFullPathNameW failed: %lu\n", GetLastError());
            return FALSE;
        }
        flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
        pwszpath = db->wszPath;
    }
    _dbglog("path='%ls' blob=%u access=0x%08X, disposition=0x%08X\n",
        pwszpath,
        dwBlobSize,
        access,
        dwCreationDisposition);
    GetSystemInfo(&db->os);
    db->os.dwPageSize = db->os.dwPageSize ? db->os.dwPageSize : 4096;
    db->os.dwAllocationGranularity = db->os.dwAllocationGranularity ? db->os.dwAllocationGranularity : 65536;
	if (dwBlobSize > MAXBLOB) {
        return FALSE;
    }
    if (dwBlobSize > 0) {
        if ((dwBlobSize % sizeof(float)) != 0) {
            fprintf(stderr, "Blob size must be a multiple of %zu (size of float32).\n", sizeof(float));
            return FALSE;
        }
    }
    memset(&db->header, 0, sizeof(db->header));
    static const char kMagic[] = "EMBEDDINGS";
    memcpy(db->header.magic, kMagic,
        (sizeof(kMagic) - 1 < sizeof(db->header.magic))
        ? (sizeof(kMagic) - 1)
        : sizeof(db->header.magic));
    db->header.version = VERSION;
    db->header.size = sizeof(FileHeader);
    db->header.alignment = db->os.dwPageSize;
    if ((dwBlobSize + sizeof(uiid)) < db->header.alignment) {
        // For small blobs, align to next power of two, minimum 64 bytes
        uint32_t align = (dwBlobSize == 0)
            ? sizeof(uiid)
            : __bytwo(dwBlobSize + sizeof(uiid)); // next power of two
        db->header.alignment = max(align, sizeof(uiid));
    }
    db->header.blobSize = dwBlobSize;
	// Header is always aligned to 4096 bytes no matter the system page size.
    if (__alignup(db->header.size, MAXHEAD) > MAXHEAD) {
        fprintf(stderr, "Invalid header size.\n");
        return FALSE;
    }
    db->hWrite = CreateFileW(pwszpath,
        access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        dwCreationDisposition,
        flags,
        NULL);
    if (!db->hWrite || db->hWrite == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateFileW failed: %lu\n", GetLastError());
        return FALSE;
    }
    OVERLAPPED ov = { 0 };
    if (!LockFileEx(db->hWrite, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXHEAD, 0, &ov)) {
        fprintf(stderr, "LockFileEx failed: %lu\n", GetLastError());
        CloseHandle(db->hWrite);
        return FALSE;
    }
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(db->hWrite, &fileSize)) {
        UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
        CloseHandle(db->hWrite);
        return FALSE;
    }
    if (fileSize.QuadPart == 0) {
		assert(db->header.size <= MAXHEAD);
        uint8_t* buff = (uint8_t*)_aligned_malloc(MAXHEAD, MAXHEAD);
        if (!buff) {
            fprintf(stderr, "Memory allocation failed\n");
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            return FALSE;
        }
        memset(buff, 0, MAXHEAD);
        errno_t err = memcpy_s(buff, MAXHEAD, &db->header, sizeof(db->header));
        if (err) {
            fprintf(stderr, "memcpy_s failed (%d)\n", err);
            _aligned_free(buff);
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            return FALSE;
        }
        LARGE_INTEGER zero = { 0 };
        if (!SetFilePointerEx(db->hWrite, zero, NULL, FILE_BEGIN)) {
            fprintf(stderr, "SetFilePointerEx failed: %lu\n", GetLastError());
            _aligned_free(buff);
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            return FALSE;
        }
        DWORD written = 0;
        if (!WriteFile(db->hWrite, buff, MAXHEAD, &written, NULL) || written != MAXHEAD) {
            fprintf(stderr, "WriteFile failed: %lu\n", GetLastError());
            _aligned_free(buff);
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            return FALSE;
        }
        FlushFileBuffers(db->hWrite);
        _aligned_free(buff);
    }
    else {
        LARGE_INTEGER zero = { 0 };
        if (!SetFilePointerEx(db->hWrite, zero, NULL, FILE_BEGIN)) {
            fprintf(stderr, "SetFilePointerEx failed: %lu\n", GetLastError());
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            return FALSE;
        }
        DWORD read = 0;
        if (!ReadFile(db->hWrite, &db->header, sizeof(db->header), &read, NULL) ||
            read != sizeof(db->header)) {
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            return FALSE;
        }
        if (memcmp(db->header.magic, kMagic, sizeof(kMagic) - 1) != 0 ||
            db->header.version != VERSION ||
            db->header.size != sizeof(FileHeader)) {
            fprintf(stderr, "Invalid or mismatched DB format\n");
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            return FALSE;
        }
        if (db->header.blobSize != dwBlobSize) {
            fprintf(stderr, "Invalid blob size.\n");
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            return FALSE;
        }
        if (db->header.alignment != db->os.dwPageSize) {
            if (db->header.alignment > db->os.dwPageSize) {
                fprintf(stderr, "Error: file created with alignment=%u (system=%u)\n",
                    db->header.alignment, db->os.dwPageSize);
                UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
                CloseHandle(db->hWrite);
                return FALSE;
            }
            fprintf(stderr, "Warning: file created with alignment=%u (system=%u)\n",
                db->header.alignment, db->os.dwPageSize);
        }
    }
    UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
    return TRUE;
}

EMBEDDINGS_API void EMBEDDINGS_CALL File_close(Embeddings* db)
{
    _dbglog("File_close();\n");
    if (!db) return;
    if (db->hWrite && db->hWrite != INVALID_HANDLE_VALUE)
        CloseHandle(db->hWrite);
    memset(db, 0, sizeof(*db));
}

#if defined(_WIN32) || defined(_WIN64)
static __forceinline BOOL _uiidcmp(const uiid* a, const uiid* b) {
    const uint64_t* pa = (const uint64_t*)a->bytes;
    const uint64_t* pb = (const uint64_t*)b->bytes;
    return (pa[0] == pb[0]) && (pa[1] == pb[1]);
}

static __forceinline void _uiidcpy(uiid* dst, const uiid* src) {
    const uint64_t* s = (const uint64_t*)src->bytes;
    uint64_t* d = (uint64_t*)dst->bytes;
    d[0] = s[0]; d[1] = s[1];
}

static inline uint64_t _uiidhash(uiid* a) {
    const uint64_t* p = (const uint64_t*)a->bytes;
    uint64_t h = p[0] + 0x9E3779B97F4A7C15ULL;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
    h = (h ^ (h >> 31)) + p[1];
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
    return h ^ (h >> 31);
}
#endif

EMBEDDINGS_API uint32_t EMBEDDINGS_CALL File_version(Embeddings* db) {
    if (!db) {
        fprintf(stderr, "The specified database pointer is NULL.\n");
        return 0;
    }
	return db->header.version;
}

EMBEDDINGS_API BOOL EMBEDDINGS_CALL File_append(Embeddings* db, uiid id, const void* blob, DWORD blobSize, BOOL bFlush) {
    if (!db) {
        fprintf(stderr, "The specified database pointer is NULL.\n");
        return FALSE;
    }
    if (db->hWrite == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "The specified database is closed or invalid.\n");
        return FALSE;
    }
    if (!blob) {
        fprintf(stderr, "The specified blob pointer is NULL.\n");
        return FALSE;
    }
    if (blobSize != db->header.blobSize) {
        fprintf(stderr,
            "The specified blob size (%u) does not match the database configuration (%u).\n",
            blobSize,
            db->header.blobSize);
        return FALSE;
    }
	size_t cc = __alignup(sizeof(uiid) + db->header.blobSize, db->header.alignment);
    uint8_t* buff = (uint8_t*)_aligned_malloc(cc, db->header.alignment);
    if (!buff) {
        fprintf(stderr, "Memory allocation failed while preparing the record buffer.\n");
        return FALSE;
    }
    memset(buff, 0, cc);
    errno_t err = memcpy_s(buff, cc, &id, sizeof(uiid));
    if (err) {
        fprintf(stderr, "memcpy_s failed (%d)\n", err);
        _aligned_free(buff);
        return FALSE;
    }
    err = memcpy_s(buff + sizeof(uiid), cc - sizeof(uiid), blob, db->header.blobSize);
    if (err) {
        fprintf(stderr, "memcpy_s failed (%d)\n", err);
        _aligned_free(buff);
        return FALSE;
    }
    LARGE_INTEGER zero = { 0 };
    if (!SetFilePointerEx(db->hWrite, zero, NULL, FILE_END)) {
        fprintf(stderr, "Failed to seek to the end of the database file (system error %lu).\n", GetLastError());
        _aligned_free(buff);
        return FALSE;
    }
    DWORD written = 0;
    BOOL bOk = WriteFile(db->hWrite, buff, cc, &written, NULL);
    _aligned_free(buff);
    if (!bOk) {
        fprintf(stderr, "Failed to append record to the database (system error %lu).\n", GetLastError());
        return FALSE;
    }
    if (written != cc) {
        fprintf(stderr, "Incomplete write: expected %lu bytes but only wrote %lu bytes.\n",
            (unsigned long)cc,
            (unsigned long)written);
        return FALSE;
    }
    if (bFlush && !FlushFileBuffers(db->hWrite)) {
        fprintf(stderr, "Failed to flush data to disk (system error %lu).\n", GetLastError());
        return FALSE;
    }
    return TRUE;
}

EMBEDDINGS_API BOOL EMBEDDINGS_CALL File_flush(Embeddings* db) {
    _dbglog("File_flush();\n");
    if (!db) {
        fprintf(stderr, "The specified database pointer is NULL.\n");
        return FALSE;
    }
    if (db->hWrite == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "The specified database is closed or invalid.\n");
        return FALSE;
    }
    if (!FlushFileBuffers(db->hWrite)) {
        fprintf(stderr, "Failed to flush data to disk (system error %lu).\n", GetLastError());
        return FALSE;
    }
    return TRUE;
}

static int __cdecl _score(const Score* a, const Score* b) {
    return (a->score < b->score) - (a->score > b->score);
}

static inline float _dotf(const float* a, const float* b, DWORD n) {
    double s = 0.0;
    for (DWORD i = 0; i < n; ++i) s += (double)a[i] * (double)b[i];
    return (float)s;
}

static inline float _normf(const float* a, DWORD n) {
    double s = 0.0;
    for (DWORD i = 0; i < n; ++i) s += (double)a[i] * (double)a[i];
    return (float)sqrt(s);
}

const float EPSILON = 1e-6f;

EMBEDDINGS_API int32_t EMBEDDINGS_CALL File_search(
    Embeddings* db,
    const float* query, uint32_t len,
    uint32_t topk,
    Score* scores,
    float min)
{
    _dbglog("File_search(min = %f);\n", min);
    if (!db) {
        fprintf(stderr, "The specified database pointer is NULL.\n");
        return -1;
    }
    if (!query) {
        fprintf(stderr, "The specified query pointer is NULL.\n");
        return -1;
    }
    if (len == 0) {
        fprintf(stderr, "The specified query length is zero.\n");
        return FALSE;
    }
    if (!db->hWrite || db->hWrite == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "The specified database is closed or invalid.\n");
        return -1;
    }
    if (topk == 0) {
        fprintf(stderr, "The specified topk value must be greater than zero.\n");
        return -1;
    }
    if (!scores) {
        fprintf(stderr, "The specified scores buffer is NULL.\n");
        return -1;
    }
    if (db->header.blobSize != len * sizeof(float)) {
        fprintf(stderr,
            "Query size (%u bytes) does not match database blob size (%u bytes).\n",
            len * (unsigned)sizeof(float),
            db->header.blobSize);
        return -1;
    }
    HANDLE hRead = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), db->hWrite, GetCurrentProcess(),
        &hRead, FILE_READ_DATA, FALSE, 0))
    {
        fprintf(stderr, "Failed to duplicate file handle for search (system error %lu).\n", GetLastError());
        return -1;
    }
    size_t cc = __alignup(sizeof(uiid) + db->header.blobSize, db->header.alignment);
    uint8_t* buff = (uint8_t*)_aligned_malloc(cc, db->header.alignment);
    if (!buff) {
        fprintf(stderr, "Memory allocation failed while preparing the read buffer.\n");
        CloseHandle(hRead);
        return -1;
    }
    LARGE_INTEGER offset = { MAXHEAD };
    if (!SetFilePointerEx(hRead, offset, NULL, FILE_BEGIN)) {
        fprintf(stderr, "Failed to seek to the first record (system error %lu).\n", GetLastError());
        _aligned_free(buff);
        CloseHandle(hRead);
        return -1;
    }
    Score* heap = (Score*)calloc(topk, sizeof(Score));
    if (!heap) {
        fprintf(stderr, "Memory allocation failed while preparing the top-k heap.\n");
        _aligned_free(buff);
        CloseHandle(hRead);
        return -1;
    }
    /* Normalize the query vector. We'll make this optional later. */
    float qnorm = _normf(query, len);
    _dbglog("qnorm = %f;\n", qnorm);
    if (qnorm < EPSILON) {
        fprintf(stderr, "Query vector norm too small (%.8g).\n", qnorm);
        free(heap);
        _aligned_free(buff);
        CloseHandle(hRead);
        return -1;
    }
    DWORD count = 0;
    for (;;) {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(hRead, buff, (DWORD)cc, &bytesRead, NULL);
        if (!ok) {
            // _dbglog("EOF\n");
            break;
        }
        if (bytesRead < cc) {
            // _dbglog("partial read expected: %lu, read: %lu\n", (unsigned long)cc, (unsigned long)bytesRead);
            break;
		}
        //  && bytesRead == cc
        const uiid* id = (const uiid*)buff;
        const float* blob = (const float*)(buff + sizeof(uiid));
        float norm = _normf(blob, len);
        // _dbglog("norm = %f;\n", norm);
        if (norm < EPSILON)
            continue;
        double dot = _dotf(blob, query, len);
        // _dbglog("dot = %f;\n", dot);
        float score = (float)(dot / ((double)qnorm * (double)norm));
        // _dbglog("score = %f;\n", score);
        if (score < min) {
            continue; /* prune below threshold */
        }
        if (count < topk) {
            _uiidcpy(&heap[count].id, id);
            heap[count].score = score;
            ++count;
            if (count == topk) {
                qsort(heap, count, sizeof(Score), _score);
            }
        }
        else if (score > heap[topk - 1].score) {
            _uiidcpy(&heap[topk - 1].id, id);
            heap[topk - 1].score = score;
            qsort(heap, count, sizeof(Score), _score);
        }
    }
    assert(count <= topk);
    /* Return in descending order */
	memset(scores, 0, topk * sizeof(Score));
    for (DWORD i = 0; i < count; ++i) {
        _uiidcpy(&scores[i].id, &heap[i].id);
		scores[i].score = heap[i].score;
    }
    free(heap);
    _aligned_free(buff);
    CloseHandle(hRead);
    _dbglog("File_search() = %d;\n", count);
    return count;
}

EMBEDDINGS_API void EMBEDDINGS_CALL Cursor_close(Cursor* cur)
{
    _dbglog("Cursor_close();\n");
    if (!cur) return;
    if (cur->buffer)
        _aligned_free(cur->buffer);
    if (cur->hReadWrite && cur->hReadWrite != INVALID_HANDLE_VALUE)
        CloseHandle(cur->hReadWrite);
    free(cur);
}

EMBEDDINGS_API BOOL EMBEDDINGS_CALL Cursor_reset(Cursor* cur) {
    _dbglog("Cursor_reset();\n");
    if (!cur) {
        fprintf(stderr, "The specified cursor pointer is NULL.\n");
        return FALSE;
    }    
    if (!cur->hReadWrite || cur->hReadWrite == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "The specified database is closed or invalid.\n");
        return FALSE;
    }
    LARGE_INTEGER offset = { MAXHEAD };
    if (!SetFilePointerEx(cur->hReadWrite, offset, NULL, FILE_BEGIN))
    {
        fprintf(stderr, "Failed to seek to the first record (system error %lu).\n", GetLastError());
        return FALSE;
    }
    return TRUE;
}

EMBEDDINGS_API Cursor* EMBEDDINGS_CALL Cursor_open(Embeddings * db, BOOL bReadOnly) {
    _dbglog("Cursor_open();\n");
    if (!db) {
        fprintf(stderr, "The specified database pointer is NULL.\n");
        return NULL;
    }
    if (!db->hWrite || db->hWrite == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "The specified database is closed or invalid.\n");
        return NULL;
    }
	Cursor* cur = (Cursor*)malloc(sizeof(Cursor));
    if (!cur) {
        fprintf(stderr, "Memory allocation failed.\n");
        return NULL;
    }
	memset(cur, 0, sizeof(*cur));
    HANDLE hReadWrite = NULL;
    if (!DuplicateHandle(
        GetCurrentProcess(),
        db->hWrite,
        GetCurrentProcess(),
        &hReadWrite,
        bReadOnly 
            ? FILE_READ_DATA // The most basic level read possible
            : FILE_READ_DATA | FILE_WRITE_DATA, // Allow updates
        FALSE,
        0))
    {
        free(cur);
        fprintf(stderr, "Failed to duplicate file handle for scanning (system error %lu).\n", GetLastError());
        return NULL;
    }
    LARGE_INTEGER offset = { MAXHEAD };
    if (!SetFilePointerEx(hReadWrite, offset, NULL, FILE_BEGIN))
    {
        free(cur);
        fprintf(stderr, "Failed to seek to the first record (system error %lu).\n", GetLastError());
        CloseHandle(hReadWrite);
        return NULL;
    }
    memcpy(&cur->header, &db->header, sizeof(FileHeader));
    cur->hReadWrite = hReadWrite;
    size_t cc = __alignup(sizeof(uiid) + cur->header.blobSize, cur->header.alignment);
    uint8_t* buffer = (uint8_t*)_aligned_malloc(cc, cur->header.alignment);
    if (!buffer) {
        fprintf(stderr, "Memory allocation failed while preparing the read buffer.\n");
        CloseHandle(hReadWrite);
        return NULL;
    }
	cur->cc = cc;
	cur->buffer = buffer;
    cur->id = (uiid*)buffer;
    cur->blob = buffer + sizeof(uiid);
	cur->blobSize = cur->header.blobSize;
    return cur;
}

EMBEDDINGS_API BOOL EMBEDDINGS_CALL Cursor_read(Cursor* cur, DWORD* err)
{
    if (err) *err = NOERROR;
    if (!cur) {
        if (err) *err = ERROR_BAD_ARGUMENTS;
        fprintf(stderr, "The specified cursor pointer is NULL.\n");
        return FALSE;
    }    
    if (!cur->buffer) {
        if (err) *err = ERROR_BAD_ARGUMENTS;
        fprintf(stderr, "The specified cursor pointer is corrupt.\n");
        return FALSE;
    }
    if (!cur->hReadWrite || cur->hReadWrite == INVALID_HANDLE_VALUE) {
        if (err) *err = ERROR_INVALID_HANDLE;
        fprintf(stderr, "The specified database is closed or invalid.\n");
        return FALSE;
    }
    cur->offset.QuadPart = 0;
    LARGE_INTEGER zero = { 0 };
    if (!SetFilePointerEx(cur->hReadWrite, zero, &cur->offset, FILE_CURRENT)) {
		DWORD sys = GetLastError();
        fprintf(stderr, "GetFilePointerEx failed. (system error %lu).\n", sys);
        if (err) *err = sys;
        return FALSE;
    }
    DWORD bytesRead = 0; BOOL ok = ReadFile(cur->hReadWrite, cur->buffer, cur->cc, &bytesRead, NULL);
    if (!ok) {
		DWORD sys = GetLastError();
        if (sys == ERROR_HANDLE_EOF || sys == ERROR_BROKEN_PIPE || sys == NO_ERROR) {
            if (err) *err = ERROR_HANDLE_EOF;
        }
        else {
            if (err) *err = sys; // True error
        }
        return FALSE;
    }
    if (bytesRead < cur->cc) { /* Partial read EOF */
        if (err) *err = ERROR_HANDLE_EOF;
        return FALSE;
    }
    return TRUE;
}

EMBEDDINGS_API BOOL EMBEDDINGS_CALL Cursor_update(Cursor* cur, uiid id, const void* blob, DWORD blobSize, BOOL bFlush) {
    if (!cur) {
        fprintf(stderr, "The specified cursor pointer is NULL.\n");
        return FALSE;
    }
    if (!cur->hReadWrite || cur->hReadWrite == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "The specified cursor is closed or invalid.\n");
        return FALSE;
    }
    if (!blob) {
        fprintf(stderr, "The specified blob pointer is NULL.\n");
        return FALSE;
    }
    if (blobSize != cur->blobSize) {
        fprintf(stderr,
            "The specified blob size (%u) does not match the database configuration (%u).\n",
            blobSize,
            cur->blobSize);
        return FALSE;
    }
    // _dbglog("Cursor_update();\n");
    OVERLAPPED ov = { 0 };
    if (!LockFileEx(cur->hReadWrite, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXHEAD, 0, &ov)) {
        fprintf(stderr, "LockFileEx failed: %lu\n", GetLastError());
        return FALSE;
    }
    LARGE_INTEGER zero = { 0 }; LARGE_INTEGER current = { 0 };
    // Remember the current file offset.
    if (!SetFilePointerEx(cur->hReadWrite, zero, &current, FILE_CURRENT)) {
        DWORD sys = GetLastError();
        fprintf(stderr, "GetFilePointerEx failed. (system error %lu).\n", sys);
        UnlockFileEx(cur->hReadWrite, 0, MAXHEAD, 0, &ov);
        return FALSE;
    }
	// Move to the record start. If doing this during the cursor read loop this is as if we are moving (-1) blob.
    if (!SetFilePointerEx(cur->hReadWrite, cur->offset, NULL, FILE_BEGIN)) {
        DWORD sys = GetLastError();
        fprintf(stderr, "SetFilePointerEx to address %08lX:%08lX failed. (system error %lu).\n", cur->offset.HighPart, cur->offset.LowPart, sys);
        UnlockFileEx(cur->hReadWrite, 0, MAXHEAD, 0, &ov);
        return FALSE;
    }
    uiid idOnDisk; DWORD bytesRead = 0; BOOL ok = ReadFile(cur->hReadWrite, &idOnDisk, sizeof(uiid), &bytesRead, NULL);
    if (!ok || bytesRead != sizeof(uiid)) {
        DWORD sys = GetLastError();
        fprintf(stderr, "ReadFile failed. (system error %lu).\n", sys);
        UnlockFileEx(cur->hReadWrite, 0, MAXHEAD, 0, &ov);
        return FALSE;
	}
	// Make sure the blob id is the expected one. The first 16 bytes are id followed by blob data.
    if (_uiidcmp(&idOnDisk, &id) != TRUE) {
        fprintf(stderr, "Record ID mismatch; expected '");
        for (int i = 0; i < 16; ++i) fprintf(stderr, "%02X", id.bytes[i]);
        fprintf(stderr, "', found '");
        for (int i = 0; i < 16; ++i) fprintf(stderr, "%02X", idOnDisk.bytes[i]);
        fprintf(stderr, "'.\n");
        UnlockFileEx(cur->hReadWrite, 0, MAXHEAD, 0, &ov);
        return FALSE;
	}
	// Update just the blob part.
    DWORD bytesWritten = 0; ok = WriteFile(cur->hReadWrite, blob, blobSize, &bytesWritten, NULL);
    if (!ok || bytesWritten != blobSize) {
        fprintf(stderr, "WriteFile failed. (system error %lu).\n", GetLastError());
        UnlockFileEx(cur->hReadWrite, 0, MAXHEAD, 0, &ov);
        return FALSE;
    }
    // Restore to where we were before.
    if (!SetFilePointerEx(cur->hReadWrite, current, NULL, FILE_BEGIN)) {
        DWORD sys = GetLastError();
        fprintf(stderr, "SetFilePointerEx failed. (system error %lu).\n", sys);
        UnlockFileEx(cur->hReadWrite, 0, MAXHEAD, 0, &ov);
        return FALSE;
    }
    if (bFlush && !FlushFileBuffers(cur->hReadWrite)) {
        fprintf(stderr, "Failed to flush data to disk (system error %lu).\n", GetLastError());
        UnlockFileEx(cur->hReadWrite, 0, MAXHEAD, 0, &ov);
        return FALSE;
    }
    // _dbglog("Cursor_update(OK);\n");
    UnlockFileEx(cur->hReadWrite, 0, MAXHEAD, 0, &ov);
    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  reason,LPVOID lpReserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        _dbglog("DLL_PROCESS_ATTACH();\n");
        break;
    case DLL_THREAD_ATTACH:
        _dbglog( "DLL_THREAD_ATTACH();\n");
        break;
    case DLL_THREAD_DETACH:
        _dbglog("DLL_THREAD_DETACH();\n");
        break;
    case DLL_PROCESS_DETACH:
        _dbglog("DLL_PROCESS_DETACH();\n");
        break;
    }
    return TRUE;
}

#if defined(PYTHON312)

#define PY_SSIZE_T_CLEAN
#include <Python.h>

typedef struct {
    PyObject_HEAD
    Embeddings db;
} PyEmbeddingsObject;


typedef struct {
    PyObject_HEAD
    Cursor* cur;
    PyObject* py_db_owner;
} PyCursorObject;


/* Forward declarations */

static PyObject* PyEmbeddings_append(PyEmbeddingsObject* self, PyObject* args, PyObject* kwds);
static void PyEmbeddings_dealloc(PyEmbeddingsObject* self);
static int PyEmbeddings_init(PyEmbeddingsObject* self, PyObject* args, PyObject* kwds);
static PyEmbeddingsObject* PyEmbeddings_new(PyTypeObject* type, PyObject* args, PyObject* kwds);
static PyObject* PyEmbeddings_open(PyObject* self, PyObject* args, PyObject* kwds);
static PyObject* PyEmbeddings_close(PyObject* obj, PyObject* ignored);
static PyObject* PyEmbeddings_flush(PyEmbeddingsObject* obj, PyObject* ignored);
static PyObject* PyEmbeddings_cursor(PyEmbeddingsObject* self, PyObject* Py_UNUSED(args));
static PyObject* PyEmbeddings_search(PyEmbeddingsObject* self, PyObject* args, PyObject* kwds);

/* Method definitions */

static PyMethodDef PyEmbeddingsMethods[] = {
    {"flush", (PyCFunction)PyEmbeddings_flush, METH_NOARGS, "Flushes the buffers and causes all buffered data to be written to a file."},
    {"close", (PyCFunction)PyEmbeddings_close, METH_NOARGS, "Close the embeddings database file and release resources."},
    {"append", (PyCFunction)PyEmbeddings_append, METH_VARARGS | METH_KEYWORDS, "Append a record to the embeddings database." },
    {"cursor",(PyCFunction)PyEmbeddings_cursor, METH_NOARGS, "Create a cursor for sequential scan."},
    {"search", (PyCFunction)PyEmbeddings_search, METH_VARARGS | METH_KEYWORDS, "Perform cosine similarity search."},
    {NULL}  /* Sentinel */
};

/* Type definition */

static PyTypeObject PyEmbeddings = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "embeddings.Embeddings",
    .tp_basicsize = sizeof(PyEmbeddingsObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyEmbeddings_new,
    .tp_init = (initproc)PyEmbeddings_init,
    .tp_dealloc = (destructor)PyEmbeddings_dealloc,
    .tp_methods = PyEmbeddingsMethods,
};

/* Module methods */

static PyMethodDef PyModuleStatic[] = {
    {"open", (PyCFunction)PyEmbeddings_open, METH_VARARGS | METH_KEYWORDS, "Open or create an embeddings database file."},
    {NULL, NULL, 0, NULL}
};

/* Module definition */

static struct PyModuleDef PyModule = {
    PyModuleDef_HEAD_INIT,
    "embeddings",
    NULL,
    -1,
    PyModuleStatic
};


/* Implementation of methods */

static void PyCursor_dealloc(PyCursorObject* self)
{
    _dbglog("PyCursor_dealloc();\n");
    if (self->cur) {
        Cursor_close(self->cur);
        self->cur = NULL;
    }
    Py_XDECREF(self->py_db_owner);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* PyCursor_tuple(Cursor* cur)
{
    // _dbglog("PyCursor_tuple();\n");
    PyObject* id = PyBytes_FromStringAndSize((const char*)cur->id, (Py_ssize_t)sizeof(uiid));
    if (!id) {
        return NULL;
    }
    PyObject* blob = PyBytes_FromStringAndSize((const char*)cur->blob, (Py_ssize_t)cur->blobSize);
    if (!blob) {
        Py_DECREF(id);
        return NULL;
    }
    PyObject* tuple = PyTuple_New(2);
    if (!tuple) {
        Py_DECREF(id);
        Py_DECREF(blob);
        return NULL;
    }
    PyTuple_SET_ITEM(tuple, 0, id);
    PyTuple_SET_ITEM(tuple, 1, blob);
    return tuple;
}

static PyObject* PyCursor_read(PyCursorObject* self, PyObject* Py_UNUSED(args))
{
    // _dbglog("PyCursor_read();\n");
    if (!self->cur) {
        PyErr_SetString(PyExc_RuntimeError, "Cursor is closed.");
        return NULL;
    }
    DWORD err;
    BOOL ok = Cursor_read(
        self->cur, &err
    );
    if (!ok) {
        if (err == ERROR_HANDLE_EOF) {
            Py_RETURN_NONE;
        }
        PyErr_Format(PyExc_OSError, "Read failed (WinError %lu)", (unsigned long)err);
        return NULL;
    }
    return PyCursor_tuple(self->cur);
}

static PyObject* PyCursor_reset(PyCursorObject* self, PyObject* Py_UNUSED(args))
{
    if (!self->cur) {
        PyErr_SetString(PyExc_RuntimeError, "Cursor is closed.");
        return NULL;
    }
    if (!Cursor_reset(self->cur)) {
        PyErr_SetString(PyExc_OSError, "Failed to reset cursor to beginning.");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* PyCursor_close(PyCursorObject* self, PyObject* Py_UNUSED(args))
{
    if (self->cur) {
        Cursor_close(self->cur);
        self->cur = NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* PyCursor_update(PyCursorObject* self, PyObject* args, PyObject* kwds)
{
    _dbglog("PyCursor_update()\n");
    PyObject* id = NULL;
    Py_buffer blob = { 0 };
    BOOL bFlush = TRUE;
    static char* kwlist[] = { "id", "blob", "flush", NULL };
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Oy*|p", kwlist, &id, &blob, &bFlush))
        return NULL;
    uiid u;
    memset(&u, 0, sizeof(u));
    /* Case 1: id is bytes */
    if (PyBytes_Check(id)) {
        Py_ssize_t len = PyBytes_Size(id);
        if (len != sizeof(u.bytes)) {
            PyErr_SetString(PyExc_ValueError, "'id' must be exactly 16 bytes");
            goto error;
        }
        memcpy(u.bytes, PyBytes_AsString(id), sizeof(u.bytes));
    }
    /* Case 2: id is uuid.UUID */
    else {
        PyObject* typename = PyObject_GetAttrString((PyObject*)Py_TYPE(id), "__name__");
        if (!typename) goto error;
        int ok = PyUnicode_Check(typename) &&
            PyUnicode_CompareWithASCIIString(typename, "UUID") == 0;
        Py_DECREF(typename);
        if (ok) {
            PyObject* b = PyObject_GetAttrString(id, "bytes");
            if (!b) goto error;
            if (!PyBytes_Check(b) || PyBytes_Size(b) != sizeof(u.bytes)) {
                PyErr_SetString(PyExc_ValueError, "'UUID.bytes' must be exactly 16 bytes");
                Py_DECREF(b);
                goto error;
            }
            memcpy(u.bytes, PyBytes_AsString(b), sizeof(u.bytes));
            Py_DECREF(b);
        }
        else {
            PyErr_SetString(PyExc_TypeError, "'id' must be bytes or uuid.UUID");
            goto error;
        }
    }
    /* Update the record */
    if (!Cursor_update(self->cur, u, blob.buf, (DWORD)blob.len, bFlush)) {
        PyErr_SetString(PyExc_OSError, "Cursor_update failed");
        goto error;
    }
    PyBuffer_Release(&blob);
    Py_RETURN_NONE;
error:
    PyBuffer_Release(&blob);
    return NULL;
}

static PyMethodDef PyCursor_methods[] = {
    {"read",  (PyCFunction)PyCursor_read,  METH_NOARGS,
     "Read the next record. Returns the record or None if EOF."},
    {"update",  (PyCFunction)PyCursor_update,  METH_VARARGS | METH_KEYWORDS,
     "Update the current record."},
    {"reset", (PyCFunction)PyCursor_reset, METH_NOARGS,
     "Rewind cursor to the first record."},
    {"close", (PyCFunction)PyCursor_close, METH_NOARGS,
     "Close the cursor and release resources."},
    {NULL, NULL, 0, NULL}
};

static PyTypeObject PyCursorType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "embeddings.Cursor",
    .tp_basicsize = sizeof(PyCursorObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Embeddings DB cursor",
    .tp_dealloc = (destructor)PyCursor_dealloc,
    .tp_methods = PyCursor_methods,
};

static PyEmbeddingsObject* PyEmbeddings_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
    _dbglog("PyEmbeddings_new();\n");
    PyEmbeddingsObject* self;
    self = (PyEmbeddingsObject*)type->tp_alloc(type, 0);
    if (self != NULL) {
		memset(&self->db, 0, sizeof(self->db));
    }
    return self;
}

static PyObject* PyEmbeddings_cursor(PyEmbeddingsObject* self, PyObject* Py_UNUSED(args)) {
    _dbglog("PyEmbeddings_cursor();\n");
    if (!self->db.hWrite || self->db.hWrite == INVALID_HANDLE_VALUE) {
        PyErr_SetString(PyExc_RuntimeError, "Database is closed.");
        return NULL;
    }
    Cursor* cur = Cursor_open(&self->db, FALSE);
    if (!cur) {
        PyErr_SetString(PyExc_OSError, "Failed to create cursor.");
        return NULL;
    }
    PyCursorObject* pycur = (PyCursorObject*)PyObject_CallObject((PyObject*)&PyCursorType, NULL);
    if (!pycur) {
        Cursor_close(cur);
        return NULL;
    }
    pycur->cur = cur;
    // Keep DB alive while cursor exists
    Py_INCREF(self);
    pycur->py_db_owner = (PyObject*)self;
    _dbglog("PyEmbeddings_cursor return();\n");
    return (PyObject*)pycur;
}

static PyObject* PyEmbeddings_close(PyObject* obj, PyObject* ignored)
{
    _dbglog("PyEmbeddings_close()\n");
    if (obj) {
        PyEmbeddingsObject* self = (PyEmbeddingsObject*)obj;
        File_close(&self->db);
        memset(&self->db, 0, sizeof(self->db));
    }
    Py_RETURN_NONE;
}

static PyObject* PyEmbeddings_flush(PyEmbeddingsObject* obj, PyObject* ignored)
{
    _dbglog("PyEmbeddings_flush()\n");
    if (!obj->db.hWrite || obj->db.hWrite == INVALID_HANDLE_VALUE) {
        PyErr_SetString(PyExc_RuntimeError, "Database is closed.");
        return NULL;
    }
    File_flush(&obj->db);
    Py_RETURN_NONE;
}

static PyObject* PyEmbeddings_open(PyObject* obj, PyObject* args, PyObject* kwds)
{
    _dbglog("PyEmbeddings_open();\n");
    PyObject* type = (PyObject*)&PyEmbeddings;
    PyObject* self = PyObject_Call(type, args, kwds);
    return self;
}

static BOOL GetFileCreationFlags(const wchar_t* pwszmode, DWORD* disposition, DWORD* access) {
    if (pwszmode == NULL || wcscmp(pwszmode, L"r") == 0) {
        *disposition = OPEN_EXISTING;
        *access = FILE_READ_DATA;
        _dbglog("access=%ls, disposition=%ls\n", L"FILE_READ_DATA", L"OPEN_EXISTING");
        return TRUE;
    }
    else if (wcscmp(pwszmode, L"a") == 0) {
        *disposition = OPEN_EXISTING;
        *access = FILE_READ_DATA | FILE_APPEND_DATA;
        _dbglog("access=%ls, disposition=%ls\n", L"FILE_READ_DATA | FILE_APPEND_DATA", L"OPEN_EXISTING");
        return TRUE;
    }
    else if (wcscmp(pwszmode, L"a+") == 0) {
        *disposition = OPEN_ALWAYS;
        *access = FILE_READ_DATA | FILE_APPEND_DATA;
        _dbglog("access=%ls, disposition=%ls\n", L"FILE_READ_DATA | FILE_APPEND_DATA", L"OPEN_ALWAYS");
        return TRUE;
    }
    else {
        return FALSE;
    }
}

static int PyEmbeddings_init(PyEmbeddingsObject* self, PyObject* args, PyObject* kwds)
{
    _dbglog("PyEmbeddings_init();\n");

    PyObject* pathobj = Py_None;
    PyObject* modeobj = Py_None;

    unsigned int dim = 0;

    static char* kwlist[] = { "path", "dim", "mode", NULL };

    /* Allow all arguments to be optional, order: path, dim, mode */
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OIO", kwlist,
        &pathobj,
        &dim,
        &modeobj))
        return -1;

    const wchar_t* pwszpath = NULL;
    const wchar_t* pwszmode = NULL;

    /* Validate and convert path */
    if (pathobj != Py_None) {
        if (!PyUnicode_Check(pathobj)) {
            PyErr_SetString(PyExc_TypeError, "'path' must be str or None");
            return -1;
        }
        pwszpath = PyUnicode_AsWideCharString(pathobj, NULL);
        if (!pwszpath)
            return -1;
    }

    /* Validate and convert mode */
    if (modeobj != Py_None) {
        if (!PyUnicode_Check(modeobj)) {
            PyErr_SetString(PyExc_TypeError, "'mode' must be str or None");
            goto error;
        }
        pwszmode = PyUnicode_AsWideCharString(modeobj, NULL);
        if (!pwszmode)
            goto error;
    }

    DWORD disposition = 0, access = 0;
    if (!GetFileCreationFlags(pwszmode, &disposition, &access)) {
        PyErr_SetString(PyExc_ValueError, "'mode' must be one of 'r', 'a', or 'a+'");
        goto error;
    }

    _dbglog("path='%ls' dim=%u mode=%ls\n",
        pwszpath ? pwszpath : L"(null)",
        dim,
        pwszmode ? pwszmode : L"(null)");

    if (!File_open(&self->db, pwszpath, access, disposition, dim * sizeof(float))) {
        PyErr_SetString(PyExc_OSError, "Embeddings_open() failed");
        goto error;
    }

    if (pwszpath) PyMem_Free((void*)pwszpath);
    if (pwszmode) PyMem_Free((void*)pwszmode);
    return 0;

error:
    if (pwszpath) PyMem_Free((void*)pwszpath);
    if (pwszmode) PyMem_Free((void*)pwszmode);
    return -1;
}


static void PyEmbeddings_dealloc(PyEmbeddingsObject* self)
{
    _dbglog("PyEmbeddings_dealloc();\n");
    File_close(&self->db);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* PyEmbeddings_append(PyEmbeddingsObject* self, PyObject* args, PyObject* kwds)
{
    // _dbglog("PyEmbeddings_append()\n");
    PyObject* id = NULL;
    Py_buffer blob = { 0 };
    static char* kwlist[] = { "id", "blob", NULL };
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "Oy*", kwlist, &id, &blob))
        return NULL;
    uiid u;
    memset(&u, 0, sizeof(u));
    /* Case 1: id is bytes */
    if (PyBytes_Check(id)) {
        Py_ssize_t len = PyBytes_Size(id);
        if (len != sizeof(u.bytes)) {
            PyErr_SetString(PyExc_ValueError, "'id' must be exactly 16 bytes");
            goto error;
        }
        memcpy(u.bytes, PyBytes_AsString(id), sizeof(u.bytes));
    }
    /* Case 2: id is uuid.UUID */
    else {
        PyObject* typename = PyObject_GetAttrString((PyObject*)Py_TYPE(id), "__name__");
        if (!typename) goto error;
        int ok = PyUnicode_Check(typename) &&
            PyUnicode_CompareWithASCIIString(typename, "UUID") == 0;
        Py_DECREF(typename);
        if (ok) {
            PyObject* b = PyObject_GetAttrString(id, "bytes");
            if (!b) goto error;
            if (!PyBytes_Check(b) || PyBytes_Size(b) != sizeof(u.bytes)) {
                PyErr_SetString(PyExc_ValueError, "'UUID.bytes' must be exactly 16 bytes");
                Py_DECREF(b);
                goto error;
            }
            memcpy(u.bytes, PyBytes_AsString(b), sizeof(u.bytes));
            Py_DECREF(b);
        }
        else {
            PyErr_SetString(PyExc_TypeError, "'id' must be bytes or uuid.UUID");
            goto error;
        }
    }
    /* Append to database */
    BOOL bFlush = FALSE;
    if (!File_append(&self->db, u, blob.buf, (DWORD)blob.len, bFlush)) {
        PyErr_SetString(PyExc_OSError, "EmbeddingsAppend failed");
        goto error;
    }
    PyBuffer_Release(&blob);
    Py_RETURN_NONE;
error:
    PyBuffer_Release(&blob);
    return NULL;
}

static PyObject* PyEmbeddings_search(PyEmbeddingsObject* self, PyObject* args, PyObject* kwds)
{
    _dbglog("PyEmbeddings_search();\n");
    static char* kwlist[] = { "query", "len", "topk", "threshold", NULL };
    Py_buffer buf;
    PyObject* len_obj = NULL;
    DWORD len = 0, topk = 0;
    float threshold = 0.0f;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y*|OIf:search", kwlist,
        &buf, &len_obj, &topk, &threshold)) {
        return NULL;
    }

    /* Validate DB handle */
    if (!self->db.hWrite || self->db.hWrite == INVALID_HANDLE_VALUE) {
        PyBuffer_Release(&buf);
        PyErr_SetString(PyExc_RuntimeError, "Database is closed or invalid.");
        return NULL;
    }

    /* Infer length if omitted */
    if (len_obj && len_obj != Py_None) {
        long tmp = PyLong_AsLong(len_obj);
        if (tmp <= 0) {
            PyBuffer_Release(&buf);
            PyErr_SetString(PyExc_ValueError, "len must be greater than zero.");
            return NULL;
        }
        len = (DWORD)tmp;
    }
    else {
        if ((buf.len % sizeof(float)) != 0) {
            PyBuffer_Release(&buf);
            PyErr_Format(PyExc_ValueError,
                "Query buffer size (%zd) is not a multiple of sizeof(float).", buf.len);
            return NULL;
        }
        len = (DWORD)(buf.len / sizeof(float));
    }

    if (topk == 0) {
        PyBuffer_Release(&buf);
        PyErr_SetString(PyExc_ValueError, "topk must be greater than zero.");
        return NULL;
    }

    if (self->db.header.blobSize != len * sizeof(float)) {
        PyBuffer_Release(&buf);
        PyErr_Format(PyExc_ValueError,
            "Query size (%u bytes) does not match database blob size (%u bytes).",
            len * (unsigned)sizeof(float),
            self->db.header.blobSize);
        return NULL;
    }

    Score* scores = (Score*)calloc(topk, sizeof(Score));
    if (!scores) {
        PyBuffer_Release(&buf);
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate score buffer.");
        return NULL;
    }

    int32_t count = File_search(&self->db,
        (const float*)buf.buf,
        len,
        topk,
        scores,
        threshold);

    PyBuffer_Release(&buf);

    if (count < 0) {
        free(scores);
        PyErr_SetString(PyExc_RuntimeError, "File_search failed.");
        return NULL;
    }

    PyObject* list = PyList_New(count);
    if (!list) {
        free(scores);
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate result list.");
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        PyObject* id_bytes = PyBytes_FromStringAndSize((const char*)&scores[i].id, sizeof(uiid));
        PyObject* score_f = PyFloat_FromDouble(scores[i].score);
        PyObject* tuple = PyTuple_Pack(2, id_bytes, score_f);
        Py_DECREF(id_bytes);
        Py_DECREF(score_f);
        PyList_SET_ITEM(list, i, tuple); /* steals ref */
    }

    free(scores);
    return list;
}

/* Module init */

PyMODINIT_FUNC PyInit_embeddings(void)
{
    _dbglog("PyInit_embeddings();\n");
    /* Ensure both types have a valid tp_new */
    PyEmbeddings.tp_new = PyType_GenericNew;
    PyCursorType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&PyEmbeddings) < 0)
        return NULL;
    if (PyType_Ready(&PyCursorType) < 0)
        return NULL;
    /* Create the module */
    PyObject* m = PyModule_Create(&PyModule);
    if (!m)
        return NULL;
    /* Add Embeddings type */
    Py_INCREF(&PyEmbeddings);
    if (PyModule_AddObject(m, "Embeddings", (PyObject*)&PyEmbeddings) < 0) {
        Py_DECREF(&PyEmbeddings);
        Py_DECREF(m);
        return NULL;
    }
    /* Add Cursor type */
    Py_INCREF(&PyCursorType);
    if (PyModule_AddObject(m, "Cursor", (PyObject*)&PyCursorType) < 0) {
        Py_DECREF(&PyCursorType);
        Py_DECREF(m);
        return NULL;
    }
    return m;
}

#endif

#ifdef CONSOLE
static int EMBEDDINGS_CALL PrintRec(const uiid* id, const void* blob, DWORD blobSize, void* u)
{
    (void)u;
    printf("id0=%02x blob0=%02x\n",
        id->bytes[0],
        ((const unsigned char*)blob)[0]);
    return 1;
}

int wmain(void)
{
    Embeddings db;
    // L"test.db"
    if (!File_open(&db, NULL, GENERIC_READ | FILE_APPEND_DATA, OPEN_ALWAYS, 512))
        goto done;

    uiid id = { 0 };
    unsigned char blob[512] = { 0 };

    for (int i = 0; i < 5; i++) {
        id.bytes[0] = (unsigned char)i;
        blob[0] = (unsigned char)(100 + i);
        File_append(&db, id, blob, sizeof(blob), TRUE);
    }

    File_scan(&db, PrintRec, NULL);

	Cursor* cur = Cursor_open(&db);
    while (Cursor_read(cur, NULL)) {
        uiid* pid = (uiid*)cur->buffer;
        void* pblob = (uint8_t*)cur->buffer + sizeof(uiid);
        printf("CUR id0=%02x blob0=%02x\n",
            pid->bytes[0],
            ((const unsigned char*)pblob)[0]);
	}
    Cursor_close(cur);

    File_close(&db);


done:
    printf("Press any key...\n");
    int ch = _getch();
    if (ch == 0 || ch == 0xE0) {
        int ext = _getch();
        printf("Extended key: 0x%02X\n", ext);
    }
    else {
        printf("Char: '%c' (0x%02X)\n", ch, ch & 0xFF);
    }
    return 0;
}
#endif
