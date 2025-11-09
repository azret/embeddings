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