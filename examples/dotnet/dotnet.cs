using embeddings;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace dotnet {

    class Demo {
        static unsafe void Main() {

            IntPtr db = Embeddings.AllocDb();

            // OPEN (like mode "a+"): GENERIC_READ | FILE_APPEND_DATA, OPEN_ALWAYS
            const uint GENERIC_READ = 0x80000000;
            const uint FILE_APPEND_DATA = 0x00000004;
            const uint OPEN_ALWAYS = 4;

            bool ok = Embeddings.Open(
                db,
                "test.db",
                GENERIC_READ | FILE_APPEND_DATA,
                OPEN_ALWAYS,
                dim: 128 /* floats per vector */
            );
            if (!ok) throw new Exception("Open failed");

            // APPEND
            float[] vec = new float[128];
            vec[0] = 1.0f;
            Uiid id = Embeddings.GuidToUiid(Guid.NewGuid());
            fixed (float* pVec = vec) {
                bool appended = Embeddings.Append(
                    db,
                    ref id,
                    pVec,
                    (uint)(vec.Length * sizeof(float)),
                    flush: false
                );
                if (!appended) throw new Exception("Append failed");
            }

            // SEARCH top-5
            fixed (float* pQuery = vec) {
                Score[] scores;
                int count = Embeddings.Search(
                    db,
                    pQuery,
                    (uint)vec.Length,
                    topk: 5,
                    threshold: 0.0f,
                    out scores
                );

                Console.WriteLine("count = " + count);
                for (int i = 0; i < count; i++) {
                    Guid gid = Embeddings.UiidToGuid(ref scores[i].id);
                    Console.WriteLine("{0}  {1}", gid, scores[i].score);
                }
            }

            // CURSOR scan (no extra copies per row)
            IntPtr cur = Embeddings.CursorOpen(db);
            uint err;
            embeddings.RecordView rec;
            while (Embeddings.CursorRead(cur, out rec, out err)) {
                Guid gid = rec.ToGuid();
                Console.WriteLine("Row guid = {0}, firstByteOfBlob = {1}",
                    gid,
                    rec.Blob[0]);
            }
            // err == ERROR_HANDLE_EOF when done.
            Embeddings.CursorClose(cur);

            // CLEANUP
            Embeddings.Close(db);
            Embeddings.FreeDb(db);
        }
    }

}
