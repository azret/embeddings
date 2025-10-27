#ifndef EMBEDDINGS_H
#define EMBEDDINGS_H

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifdef EMBEDDINGS_C
#define EMBEDDINGS_API __declspec(dllexport)
#else
#define EMBEDDINGS_API __declspec(dllimport)
#endif
#define EMBEDDINGS_CALL __stdcall
#else
#define EMBEDDINGS_API
#define EMBEDDINGS_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
    typedef struct uiid {
        unsigned char bytes[16];
    } uiid;
#pragma pack(pop)

#pragma pack(push, 1)
    typedef struct FileHeader {
        char magic[0x10];
        uint32_t version;
        uint32_t size;
        uint32_t alignment;
        uint32_t blobSize;
        uint8_t dtype;
    } FileHeader;
#pragma pack(pop)

    #define PATH 1024

#pragma pack(push, 1)
    typedef struct Embeddings {
        HANDLE hWrite;
        FileHeader header;
        SYSTEM_INFO os;
        wchar_t wszPath[PATH];
    } Embeddings;
#pragma pack(pop)

    EMBEDDINGS_API BOOL EMBEDDINGS_CALL File_open(Embeddings* db, const wchar_t* szPath, DWORD access, DWORD dwCreationDisposition, uint32_t dwBlobSize);
    EMBEDDINGS_API BOOL EMBEDDINGS_CALL File_append(Embeddings* db, uiid id, const void* blob, DWORD blobSize, BOOL bFlush);
    EMBEDDINGS_API BOOL EMBEDDINGS_CALL File_flush(Embeddings* db);
    EMBEDDINGS_API void EMBEDDINGS_CALL File_close(Embeddings* db);
    EMBEDDINGS_API uint32_t EMBEDDINGS_CALL File_version(Embeddings* db);

#pragma pack(push, 1)
    typedef struct {
        uiid id;
        float score;
    } Score;
#pragma pack(pop)

    EMBEDDINGS_API int32_t EMBEDDINGS_CALL File_search(
        Embeddings* db,
        const float* query, uint32_t len,
        uint32_t topk,
        Score* scores,
        float min);

#pragma pack(push, 1)
    typedef struct Cursor {
        HANDLE hReadWrite;
        FileHeader header;
        LARGE_INTEGER offset;
        uint32_t cc;
        void* buffer;
        uiid* id;
        uint8_t* blob;
        uint32_t blobSize;
    } Cursor;
#pragma pack(pop)

    EMBEDDINGS_API Cursor* EMBEDDINGS_CALL Cursor_open(Embeddings* db, BOOL bReadOnly);
    EMBEDDINGS_API void EMBEDDINGS_CALL Cursor_close(Cursor* cur);
    EMBEDDINGS_API BOOL EMBEDDINGS_CALL Cursor_reset(Cursor* cur);
    EMBEDDINGS_API BOOL EMBEDDINGS_CALL Cursor_read(Cursor* cur, DWORD* err);
    EMBEDDINGS_API BOOL EMBEDDINGS_CALL Cursor_update(Cursor* cur, uiid id, const void* blob, DWORD blobSize, BOOL bFlush);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDDINGS_H */
