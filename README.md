# embeddings.c

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Build](https://github.com/azret/embeddings/actions/workflows/ci.yml/badge.svg)](https://github.com/azret/embeddings/actions)

A tiny binary store for fixed length **F32** embeddings.

### File Format

The file consists of a header followed by a sequence of records. Each record is composed of a unique identifier (UUID - 16 bytes) and a binary blob.

**| HEADER : UIID : BLOB : UUID : BLOB : ... |** 

### Implementation Details

Naive pure **"C"** implementation with no external dependencies designed to run on a **CPU**.

A lot of room for optimizations and improvements, but it works great for small to medium indexes.

The search is performed via a linear scan of the file, computing the distance score between the query and each stored vector.

If the dimensions are too big or the dataset is too large, consider using more advanced libraries.

### Roadmap

In general need to make this much faster beyond the naive implementation currently available.

- [x] Basic append-only storage
- [ ] Intel AVX/AVX2/AVX512 optimizations for faster search
- [ ] Multi-threaded search
- [ ] Indexing for faster search (HNSW, IVF, PQ, etc)
- [ ] Support for other distance metrics (Euclidean, Manhattan, etc)
- [ ] Support for other data types (FP16, INT8, etc)

### Bindings

- Python
- C#

### Quick start

```
import uuid, array, embeddings

# Create or open an embeddings database

db = embeddings.Embeddings(path=":temp:", dim=768, mode="a+")

db.append(array.array("b", [1] * 16).tobytes(), array.array("f", [1.0] * 768).tobytes())
db.append(array.array("b", [2] * 16).tobytes(), array.array("f", [2.0] * 768).tobytes())
db.append(array.array("b", [3] * 16).tobytes(), array.array("f", [3.0] * 768).tobytes())
db.append(array.array("b", [4] * 16).tobytes(), array.array("f", [4.0] * 768).tobytes())
db.append(array.array("b", [5] * 16).tobytes(), array.array("f", [5.0] * 768).tobytes())

db.flush()

# Search for topk similar vectors

query = array.array("f", [3.0] * 768).tobytes()

hits = db.search(query, topk=10, threshold=0.25, norm=True)

for id, score in hits:
    print(f"id: {id}, score: {score}")

# Scan & in-place update

cur = db.cursor()

while True:
    rec = cur.read()
    if rec is None:
        break
    id, blob = rec
    cur.update(id, blob, flush=True)

db.close()
```

### Building from source code (Windows Only)

Clone the repository and build:

```bash
git clone
cd embeddings
build embeddings.sln
```

Build your wheel:

```bash
python -m build --wheel --outdir dist/
```

That creates:

```bash
dist/embeddings-1.0-py3-none-any.whl
```

You can test it:

```bash
pip install dist/embeddings-1.0-py3-none-any.whl
```

Then verify:

```bash
python -c "import embeddings; print(embeddings)"
```
