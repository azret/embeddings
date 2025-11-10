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
#define _dbglog(...) ((void)0)
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

static inline uint32_t powoftwo(uint32_t x)
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

EMBEDDINGS_API Embeddings* EMBEDDINGS_CALL fileopen(
    const wchar_t* pwszpath, DWORD dwAccess, DWORD dwCreationDisposition, uint32_t dwBlobSize)
{
	Embeddings* db = malloc(sizeof(Embeddings));
    _dbglog(">> fileopen(path='%ls' blob=%u access=0x%08X, disposition=0x%08X);\n", pwszpath, dwBlobSize, dwAccess, dwCreationDisposition);
    memset(db, 0, sizeof(*db));
    DWORD flags;
    assert(PATH >= MAX_PATH);
    if (!pwszpath || wcscmp(pwszpath, L":temp:") == 0) {
        wchar_t userTempFolder[PATH];
        GetTempPathW(PATH, userTempFolder);
        if (!GetTempFileNameW(userTempFolder, L"embeddings", 0, db->wszPath)) {
            free(db);
            fprintf(stderr, "Failed to create a temporary file name: %lu\n", GetLastError());
            return NULL;
        }
        flags = FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_SEQUENTIAL_SCAN;
		dwCreationDisposition = CREATE_ALWAYS;
        dwAccess = FILE_READ_DATA | FILE_APPEND_DATA | FILE_WRITE_DATA;
		pwszpath = db->wszPath;
    } else {
        if (!GetFullPathNameW(pwszpath, PATH, db->wszPath, NULL)) {
            free(db);
            fprintf(stderr, "GetFullPathNameW failed: %lu\n", GetLastError());
            return NULL;
        }
        flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
        pwszpath = db->wszPath;
    }
    _dbglog("path='%ls' blob=%u access=0x%08X, disposition=0x%08X\n",
        pwszpath,
        dwBlobSize,
        dwAccess,
        dwCreationDisposition);
    GetSystemInfo(&db->os);
    db->os.dwPageSize = db->os.dwPageSize ? db->os.dwPageSize : 4096;
    db->os.dwAllocationGranularity = db->os.dwAllocationGranularity ? db->os.dwAllocationGranularity : 65536;
	if (dwBlobSize > MAXBLOB) {
        free(db);
        fprintf(stderr, "The specified blob size %lu is invalid. Maximum blob size is %lu.\n", dwBlobSize, MAXBLOB);
        return NULL;
    }
    if (dwBlobSize > 0) {
        if ((dwBlobSize % sizeof(float)) != 0) {
            free(db);
            fprintf(stderr, "Blob size must be a multiple of %zu (size of float32).\n", sizeof(float));
            return NULL;
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
            : powoftwo(dwBlobSize + sizeof(uiid));
        /* ensure power-of-two minimum of 64 */
        align = (align < 64) ? 64 : align;
        /* align MUST be power-of-two; if you change sizeof(uiid), re-assert here */
        assert((align & (align - 1)) == 0);
        db->header.alignment = align;
    }
    db->header.blobSize = dwBlobSize;
	// Header is always aligned to 4096 bytes no matter the system page size.
    if (__alignup(db->header.size, MAXHEAD) > MAXHEAD) {
        free(db);
        fprintf(stderr, "Invalid header size.\n");
        return NULL;
    }
	db->access = dwAccess;
	db->dwCreationDisposition = dwCreationDisposition;
    db->hWrite = CreateFileW(pwszpath,
        dwAccess,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        dwCreationDisposition,
        flags,
        NULL);
    if (!db->hWrite || db->hWrite == INVALID_HANDLE_VALUE) {
        free(db);
        fprintf(stderr, "CreateFileW failed: %lu\n", GetLastError());
        return NULL;
    }
    OVERLAPPED ov = { 0 };
    if (!LockFileEx(db->hWrite, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXHEAD, 0, &ov)) {
        CloseHandle(db->hWrite);
        free(db);
        fprintf(stderr, "LockFileEx failed: %lu\n", GetLastError());
        return NULL;
    }
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(db->hWrite, &fileSize)) {
        UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
        CloseHandle(db->hWrite);
        free(db);
        return NULL;
    }
    if (fileSize.QuadPart == 0) {
		assert(db->header.size <= MAXHEAD);
        uint8_t* buff = (uint8_t*)_aligned_malloc(MAXHEAD, MAXHEAD);
        if (!buff) {
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            free(db);
            fprintf(stderr, "Memory allocation failed\n");
            return NULL;
        }
        memset(buff, 0, MAXHEAD);
        errno_t err = memcpy_s(buff, MAXHEAD, &db->header, sizeof(db->header));
        if (err) {
            _aligned_free(buff);
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            free(db);
            fprintf(stderr, "memcpy_s failed (%d)\n", err);
            return NULL;
        }
        LARGE_INTEGER zero = { 0 };
        if (!SetFilePointerEx(db->hWrite, zero, NULL, FILE_BEGIN)) {
            _aligned_free(buff);
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            free(db);
            fprintf(stderr, "SetFilePointerEx failed: %lu\n", GetLastError());
            return NULL;
        }
        DWORD written = 0;
        if (!WriteFile(db->hWrite, buff, MAXHEAD, &written, NULL) || written != MAXHEAD) {
            fprintf(stderr, "WriteFile failed: %lu\n", GetLastError());
            _aligned_free(buff);
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            free(db);
            return NULL;
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
            free(db);
            return NULL;
        }
        DWORD read = 0;
        if (!ReadFile(db->hWrite, &db->header, sizeof(db->header), &read, NULL) ||
            read != sizeof(db->header)) {
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            free(db);
            return NULL;
        }
        if (memcmp(db->header.magic, kMagic, sizeof(kMagic) - 1) != 0 ||
            db->header.version != VERSION ||
            db->header.size != sizeof(FileHeader)) {
            fprintf(stderr, "Invalid or mismatched DB format\n");
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            free(db);
            return NULL;
        }
        if (db->header.blobSize != dwBlobSize) {
            fprintf(stderr, "Invalid blob size.\n");
            UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
            CloseHandle(db->hWrite);
            free(db);
            return NULL;
        }
        if (db->header.alignment != db->os.dwPageSize) {
            if (db->header.alignment > db->os.dwPageSize) {
                fprintf(stderr, "Error: file created with alignment=%u (system=%u)\n",
                    db->header.alignment, db->os.dwPageSize);
                UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
                CloseHandle(db->hWrite);
                free(db);
                return NULL;
            }
            fprintf(stderr, "Warning: file created with alignment=%u (system=%u)\n",
                db->header.alignment, db->os.dwPageSize);
        }
    }
    UnlockFileEx(db->hWrite, 0, MAXHEAD, 0, &ov);
    return db;
}

EMBEDDINGS_API void EMBEDDINGS_CALL fileclose(Embeddings* db)
{
    _dbglog("fileclose();\n");
    if (!db) return;
    if (db->hWrite && db->hWrite != INVALID_HANDLE_VALUE)
        CloseHandle(db->hWrite);
    free(db);
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

EMBEDDINGS_API uint32_t EMBEDDINGS_CALL fileversion(Embeddings* db) {
    if (!db) {
        fprintf(stderr, "The specified database pointer is NULL.\n");
        return 0;
    }
	return db->header.version;
}

//  Warning: Does not lock. Assumes FILE_APPEND_DATA.
EMBEDDINGS_API BOOL EMBEDDINGS_CALL fileappend(Embeddings* db, uiid id, const void* blob, DWORD blobSize, BOOL bFlush) {
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
    // TODO : OP (0: Add, 1, Delete, 2 Update)
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
    if (db->access & FILE_APPEND_DATA) {
        /* Move to end is automatic */
    }
    else {
        fprintf(stderr, "WARNING: File was open without FILE_APPEND_DATA. Performing explicit seek to EOF.\n");
        LARGE_INTEGER zero = { 0 };
        if (!SetFilePointerEx(db->hWrite, zero, NULL, FILE_END)) {
            fprintf(stderr, "Failed to seek to the end of the database file (system error %lu).\n", GetLastError());
            _aligned_free(buff);
            return FALSE;
        }
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

EMBEDDINGS_API BOOL EMBEDDINGS_CALL fileflush(Embeddings* db) {
    _dbglog("fileflush();\n");
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

static inline float cblas_sdot(const float* a, const float* b, uint32_t n) {
    double s = 0.0;
    for (uint32_t i = 0; i < n; ++i) s += (double)a[i] * (double)b[i];
    return (float)s;
}

static inline float cblas_snrm2(const float* a, uint32_t n) {
    double s = 0.0;
    for (uint32_t i = 0; i < n; ++i) s += (double)a[i] * (double)a[i];
    return (float)sqrt(s);
}

const float EPSILON = 1e-6f;

static int __cdecl heap_qsort_func(const void* pa, const void* pb)
{
    const Score* a = (const Score*)pa;
    const Score* b = (const Score*)pb;
    return (a->score < b->score) - (a->score > b->score);
}

static int find_in_heap(const Score* heap, size_t num, const uiid* id)
{
    for (size_t i = 0; i < num; ++i) {
        if (_uiidcmp(&heap[i].id, id)) {
            return (int)i;
        }
    }
    return -1;
}

static void remove_from_heap(Score* heap, size_t* num, size_t idx)
{
    if (idx >= *num) return;
    for (size_t i = idx; i + 1 < *num; ++i) {
        heap[i] = heap[i + 1];
    }
    (*num)--;
}

static void remove_from_heap_if(Score* heap, size_t* num, const uiid* id)
{
    int idx = find_in_heap(heap, *num, id);
    if (idx >= 0) {
        remove_from_heap(heap, num, (size_t)idx);
    }
}

void cosine(const float* query, uint32_t len,
    float qnorm,
    const uint8_t* buff,
    float min,
    size_t* num,
    uint32_t topk,
    Score* heap,
    BOOL bNorm)
{
    const uiid* id = (const uiid*)buff;
    const float* blob = (const float*)(buff + sizeof(uiid));
    float norm = bNorm
        ? cblas_snrm2(blob, len)
        : 1;
    if (norm < EPSILON) {
        return;
    }
    remove_from_heap_if(
        heap,
        num,
        id
    );
    double dot = cblas_sdot(blob, query, len);
    float score = (float)(dot / ((double)qnorm * (double)norm));
    if (score >= min) {
        if (*num < topk) {
            // start accumulating until we fill the heap
            _uiidcpy(&heap[*num].id, id);
            heap[*num].score = score;
            (*num) = (*num) + 1;
            qsort(heap, *num, sizeof(Score), heap_qsort_func);
        }
        else if (score > heap[topk - 1].score) {
            // evict the lowest score
            _uiidcpy(&heap[topk - 1].id, id);
            heap[topk - 1].score = score;
            qsort(heap, *num, sizeof(Score), heap_qsort_func);
        }
    }
}

EMBEDDINGS_API int32_t EMBEDDINGS_CALL filesearch(
    Embeddings* db,
    const float* query, uint32_t len,
    uint32_t topk,
    Score* scores,
    float min,
    BOOL bNorm)
{
    _dbglog("filesearch(min = %f);\n", min);
    if (!db) {
        fprintf(stderr, "The specified database pointer is NULL.\n");
        return -1;
    }
    if (!query) {
        fprintf(stderr, "The specified query pointer is NULL.\n");
        return -1;
    }
    float qnorm = bNorm
        ? cblas_snrm2(query, len)
        : 1;
    _dbglog("qnorm = %f;\n", qnorm);
    if (qnorm < EPSILON) {
        fprintf(stderr, "Query vector norm too small (%.8g).\n", qnorm);
        return -1;
    }
    if (len == 0) {
        fprintf(stderr, "The specified query length is zero.\n");
        return -1;
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
    LARGE_INTEGER offset = { MAXHEAD };
    if (!SetFilePointerEx(hRead, offset, NULL, FILE_BEGIN)) {
        fprintf(stderr, "Failed to seek to the first record (system error %lu).\n", GetLastError());
        CloseHandle(hRead);
        return -1;
    }
    Score* heap = (Score*)calloc(topk, sizeof(Score));
    if (!heap) {
        fprintf(stderr, "Memory allocation failed while preparing the top-k heap.\n");
        CloseHandle(hRead);
        return -1;
    }
    uint32_t stride = __alignup(sizeof(uiid) + db->header.blobSize, db->header.alignment);
    uint8_t* carry = (uint8_t*)malloc(stride);
    if (!carry) {
        fprintf(stderr, "Memory allocation failed while preparing the read buffers.\n");
        free(heap);
        CloseHandle(hRead);
        return -1;
    }
    const uint32_t MAX = 1024;
    uint8_t* big = (uint8_t*)_aligned_malloc((size_t)(MAX * stride), db->header.alignment);
    if (!big) {
        fprintf(stderr, "Memory allocation failed while preparing the read buffers.\n");
        free(heap);
        free(carry);
        CloseHandle(hRead);
        return -1;
    }
    size_t num = 0; size_t leftoverBytes = 0;
    for (;;) {
        if (leftoverBytes) {
            memcpy(big, carry, leftoverBytes);
        }
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(hRead, big + leftoverBytes, (DWORD)(MAX * stride - leftoverBytes), &bytesRead, NULL);
        if (!ok || bytesRead == 0) {
            break; // EOF
        }
        size_t total = leftoverBytes + bytesRead;
        size_t offset = 0;
        while (offset + stride <= total) {
            cosine(
                query,
                len,
                qnorm,
                (uint8_t*)big + offset,
                min,
                &num,
                topk,
                heap,
                bNorm);
            offset += stride;
        } 
        leftoverBytes = total - offset;
        if (leftoverBytes) {
            memcpy(carry, big + offset, leftoverBytes);
        }
    }
    assert(num <= topk);
	memset(scores, 0, topk * sizeof(Score));
    for (DWORD i = 0; i < num; ++i) {
        _uiidcpy(&scores[i].id, &heap[i].id);
		scores[i].score = heap[i].score;
    }
    _aligned_free(big);
    free(carry);
    free(heap);
    CloseHandle(hRead);
    _dbglog("filesearch() = %u;\n", (unsigned int)num);
    return num;
}

/* Cursor API is desined for offline processing. It should not be used on a live index for upserting. */

EMBEDDINGS_API void EMBEDDINGS_CALL cursorclose(Cursor* cur)
{
    _dbglog("Cursor_close();\n");
    if (!cur) return;
    if (cur->buffer)
        _aligned_free(cur->buffer);
    if (cur->hReadWrite && cur->hReadWrite != INVALID_HANDLE_VALUE)
        CloseHandle(cur->hReadWrite);
    free(cur);
}

EMBEDDINGS_API BOOL EMBEDDINGS_CALL cursorreset(Cursor* cur) {
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

EMBEDDINGS_API Cursor* EMBEDDINGS_CALL cursoropen(Embeddings * db, BOOL bReadOnly) {
    _dbglog("cursoropen(bReadOnly=%d);\n", bReadOnly);
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

EMBEDDINGS_API BOOL EMBEDDINGS_CALL cursorread(Cursor* cur, DWORD* err)
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

EMBEDDINGS_API BOOL EMBEDDINGS_CALL cursorupdate(Cursor* cur, uiid id, const void* blob, DWORD blobSize, BOOL bFlush) {
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
    Embeddings* db;
} PyEmbeddingsObject;


typedef struct {
    PyObject_HEAD
    Cursor* cur;
    PyObject* py_db_owner;
} PyCursorObject;


/* Forward declarations */

static PyObject* PyEmbeddings_Append(PyEmbeddingsObject* self, PyObject* args, PyObject* kwds);
static void PyEmbeddings_Dealloc(PyEmbeddingsObject* self);
static int PyEmbeddings_Init(PyEmbeddingsObject* self, PyObject* args, PyObject* kwds);
static PyEmbeddingsObject* PyEmbeddings_New(PyTypeObject* type, PyObject* args, PyObject* kwds);
static PyObject* PyEmbeddings_Open(PyObject* self, PyObject* args, PyObject* kwds);
static PyObject* PyEmbeddings_Close(PyObject* obj, PyObject* ignored);
static PyObject* PyEmbeddings_Flush(PyEmbeddingsObject* obj, PyObject* ignored);
static PyObject* PyEmbeddings_Cursor(PyEmbeddingsObject* self, PyObject* Py_UNUSED(args));
static PyObject* PyEmbeddings_Search(PyEmbeddingsObject* self, PyObject* args, PyObject* kwds);

/* Method definitions */

static PyMethodDef PyEmbeddingsMethods[] = {
    {"flush", (PyCFunction)PyEmbeddings_Flush, METH_NOARGS, "Flushes the buffers and causes all buffered data to be written to a file."},
    {"close", (PyCFunction)PyEmbeddings_Close, METH_NOARGS, "Close the embeddings database file and release resources."},
    {"append", (PyCFunction)PyEmbeddings_Append, METH_VARARGS | METH_KEYWORDS, "Append a record to the embeddings database." },
    {"cursor",(PyCFunction)PyEmbeddings_Cursor, METH_NOARGS, "Create a cursor for sequential scan."},
    {"search", (PyCFunction)PyEmbeddings_Search, METH_VARARGS | METH_KEYWORDS, "Perform cosine similarity search."},
    {NULL}  /* Sentinel */
};

/* Type definition */

static PyTypeObject PyEmbeddings = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "embeddings.Embeddings",
    .tp_basicsize = sizeof(PyEmbeddingsObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyEmbeddings_New,
    .tp_init = (initproc)PyEmbeddings_Init,
    .tp_dealloc = (destructor)PyEmbeddings_Dealloc,
    .tp_methods = PyEmbeddingsMethods,
};

/* Module methods */

static PyMethodDef PyModuleStatic[] = {
    {"open", (PyCFunction)PyEmbeddings_Open, METH_VARARGS | METH_KEYWORDS, "Open or create an embeddings database file."},
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

static void PyCursor_Dealloc(PyCursorObject* self)
{
    _dbglog("PyCursor_Dealloc();\n");
    if (self->cur) {
        cursorclose(self->cur);
        self->cur = NULL;
    }
    Py_XDECREF(self->py_db_owner);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* PyCursor_Tuple(Cursor* cur)
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
    BOOL ok = cursorread(
        self->cur, &err
    );
    if (!ok) {
        if (err == ERROR_HANDLE_EOF) {
            Py_RETURN_NONE;
        }
        PyErr_Format(PyExc_OSError, "Read failed (WinError %lu)", (unsigned long)err);
        return NULL;
    }
    return PyCursor_Tuple(self->cur);
}

static PyObject* PyCursorReset(PyCursorObject* self, PyObject* Py_UNUSED(args))
{
    if (!self->cur) {
        PyErr_SetString(PyExc_RuntimeError, "Cursor is closed.");
        return NULL;
    }
    if (!cursorreset(self->cur)) {
        PyErr_SetString(PyExc_OSError, "Failed to reset cursor to beginning.");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* PyCursorClose(PyCursorObject* self, PyObject* Py_UNUSED(args))
{
    if (self->cur) {
        cursorclose(self->cur);
        self->cur = NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* PyCursorUpdate(PyCursorObject* self, PyObject* args, PyObject* kwds)
{
    // _dbglog("PyCursor_update()\n");
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
    if (!cursorupdate(self->cur, u, blob.buf, (DWORD)blob.len, bFlush)) {
        PyErr_SetString(PyExc_OSError, "Cursor_update failed");
        goto error;
    }
    PyBuffer_Release(&blob);
    Py_RETURN_NONE;
error:
    PyBuffer_Release(&blob);
    return NULL;
}

static PyMethodDef PyCursorMethods[] = {
    {"read",  (PyCFunction)PyCursor_read,  METH_NOARGS,
     "Read the next record. Returns the record or None if EOF."},
    {"update",  (PyCFunction)PyCursorUpdate,  METH_VARARGS | METH_KEYWORDS,
     "Update the current record."},
    {"reset", (PyCFunction)PyCursorReset, METH_NOARGS,
     "Rewind cursor to the first record."},
    {"close", (PyCFunction)PyCursorClose, METH_NOARGS,
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
    .tp_dealloc = (destructor)PyCursor_Dealloc,
    .tp_methods = PyCursorMethods,
};

static PyEmbeddingsObject* PyEmbeddings_New(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
    _dbglog("PyEmbeddings_New();\n");
    PyEmbeddingsObject* self;
    self = (PyEmbeddingsObject*)type->tp_alloc(type, 0);
    if (self) {
        self->db = NULL;
    }
    return self;
}

static PyObject* PyEmbeddings_Cursor(PyEmbeddingsObject* self, PyObject* Py_UNUSED(args)) {
    _dbglog("PyEmbeddings_Cursor();\n");
    if (!self->db->hWrite || self->db->hWrite == INVALID_HANDLE_VALUE) {
        PyErr_SetString(PyExc_RuntimeError, "Database is closed.");
        return NULL;
    }
    Cursor* cur = cursoropen(self->db, FALSE);
    if (!cur) {
        PyErr_SetString(PyExc_OSError, "Failed to create cursor.");
        return NULL;
    }
    PyCursorObject* pycur = (PyCursorObject*)PyObject_CallObject((PyObject*)&PyCursorType, NULL);
    if (!pycur) {
        cursorclose(cur);
        return NULL;
    }
    pycur->cur = cur;
    // Keep DB alive while cursor exists
    Py_INCREF(self);
    pycur->py_db_owner = (PyObject*)self;
    _dbglog("PyEmbeddings_Cursor return();\n");
    return (PyObject*)pycur;
}

static PyObject* PyEmbeddings_Close(PyObject* obj, PyObject* ignored)
{
    _dbglog("PyEmbeddings_close()\n");
    if (obj) {
        PyEmbeddingsObject* self = (PyEmbeddingsObject*)obj;
        fileclose(self->db);
        self->db = NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* PyEmbeddings_Flush(PyEmbeddingsObject* obj, PyObject* ignored)
{
    _dbglog("PyEmbeddings_flush()\n");
    if (!obj->db->hWrite || obj->db->hWrite == INVALID_HANDLE_VALUE) {
        PyErr_SetString(PyExc_RuntimeError, "Database is closed.");
        return NULL;
    }
    fileflush(obj->db);
    Py_RETURN_NONE;
}

static PyObject* PyEmbeddings_Open(PyObject* obj, PyObject* args, PyObject* kwds)
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
        // Append to an existing file. Fail if does not exist.
        *disposition = OPEN_EXISTING;
        *access = FILE_READ_DATA | FILE_APPEND_DATA | FILE_WRITE_DATA;
        _dbglog("access=%ls, disposition=%ls\n", L"FILE_READ_DATA | FILE_APPEND_DATA", L"OPEN_EXISTING");
        return TRUE;
    }
    else if (wcscmp(pwszmode, L"a+") == 0) {
        // Append to an existing file. Create a new one if does not exist.
        *disposition = OPEN_ALWAYS;
        *access = FILE_READ_DATA | FILE_APPEND_DATA | FILE_WRITE_DATA;
        _dbglog("access=%ls, disposition=%ls\n", L"FILE_READ_DATA | FILE_APPEND_DATA", L"OPEN_ALWAYS");
        return TRUE;
    }
    else if (wcscmp(pwszmode, L"a++") == 0) {
        // Create a new one awlays.
        *disposition = CREATE_ALWAYS;
        *access = FILE_READ_DATA | FILE_APPEND_DATA | FILE_WRITE_DATA;
        _dbglog("access=%ls, disposition=%ls\n", L"FILE_READ_DATA | FILE_APPEND_DATA", L"CREATE_ALWAYS");
        return TRUE;
    }
    else {
        return FALSE;
    }
}

static int PyEmbeddings_Init(PyEmbeddingsObject* self, PyObject* args, PyObject* kwds)
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

    if (!(self->db = fileopen(pwszpath, access, disposition, dim * sizeof(float)))) {
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


static void PyEmbeddings_Dealloc(PyEmbeddingsObject* self)
{
    _dbglog("PyEmbeddings_Dealloc();\n");
    fileclose(self->db);
    self->db = NULL;
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* PyEmbeddings_Append(PyEmbeddingsObject* self, PyObject* args, PyObject* kwds)
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
    if (!fileappend(self->db, u, blob.buf, (DWORD)blob.len, bFlush)) {
        PyErr_SetString(PyExc_OSError, "EmbeddingsAppend failed");
        goto error;
    }
    PyBuffer_Release(&blob);
    Py_RETURN_NONE;
error:
    PyBuffer_Release(&blob);
    return NULL;
}

static PyObject* PyEmbeddings_Search(PyEmbeddingsObject* self, PyObject* args, PyObject* kwds)
{
    _dbglog("PyEmbeddings_search();\n");
    static char* kwlist[] = { "query", "len", "topk", "threshold", "norm", NULL };
    Py_buffer buf;
    PyObject* len_obj = NULL;
    DWORD len = 0, topk = 0;
    float threshold = 0.0f;
	int norm = 1; // Normalize by default
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y*|OIfp:search", kwlist,
        &buf, &len_obj, &topk, &threshold, &norm)) {
        return NULL;
    }

    /* Validate DB handle */
    if (!self->db->hWrite || self->db->hWrite == INVALID_HANDLE_VALUE) {
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
        if (buf.len != (Py_ssize_t)(len * sizeof(float))) {
            PyBuffer_Release(&buf);
            PyErr_Format(PyExc_ValueError, "Buffer size (%zd) does not match provided len (%u * %zu = %u).", buf.len, len, sizeof(float), len * (unsigned)sizeof(float));
            return NULL;
        }
    }

    if (topk == 0) {
        PyBuffer_Release(&buf);
        PyErr_SetString(PyExc_ValueError, "topk must be greater than zero.");
        return NULL;
    }

    if (self->db->header.blobSize != len * sizeof(float)) {
        PyBuffer_Release(&buf);
        PyErr_Format(PyExc_ValueError,
            "Query size (%u bytes) does not match database blob size (%u bytes).",
            len * (unsigned)sizeof(float),
            self->db->header.blobSize);
        return NULL;
    }

    Score* scores = (Score*)calloc(topk, sizeof(Score));
    if (!scores) {
        PyBuffer_Release(&buf);
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate score buffer.");
        return NULL;
    }

    int32_t count = filesearch(self->db,
        (const float*)buf.buf,
        len,
        topk,
        scores,
        threshold,
        norm);

    PyBuffer_Release(&buf);

    if (count < 0) {
        free(scores);
        PyErr_SetString(PyExc_RuntimeError, "filesearch failed.");
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
