using embeddings;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace dotnet {

    class Demo {
        static unsafe void Main() {

            Console.WriteLine(Intel.mkl.Version.ToString());

            var g = Guid.NewGuid();
            Uiid u = Uiid.FromGuid(g);
            Debug.Assert(u.ToGuid() == g);

            IntPtr db = Embeddings.Open(
                ":temp:",
                "a++",
                dim: 128
            );

            if (db == IntPtr.Zero) throw new Exception("Open failed");

            // APPEND
            float[] vec = new float[128];
            vec[0] = 1.0f;
            Uiid id = Uiid.FromGuid(Guid.NewGuid());
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
                    Guid gid = scores[i].id.ToGuid();
                    Console.WriteLine("{0}  {1}", gid, scores[i].score);
                }
            }

            // // CURSOR scan (no extra copies per row)
            // IntPtr cur = Embeddings.CursorOpen(db);
            // uint err;
            // embeddings.RecordView rec;
            // while (Embeddings.CursorRead(cur, out rec, out err)) {
            //     Guid gid = rec.ToGuid();
            //     Console.WriteLine("Row guid = {0}, firstByteOfBlob = {1}",
            //         gid,
            //         rec.Blob[0]);
            // }
            // // err == ERROR_HANDLE_EOF when done.
            // Embeddings.CursorClose(cur);

            // CLEANUP
            Embeddings.Close(db);
        }
    }

}
