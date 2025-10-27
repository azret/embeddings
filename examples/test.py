# python -m examples.test

import os, uuid, random, numpy

import sys, os

for root, dirs, files in os.walk(sys.prefix):
    for f in files:
        if f.lower() == "mkl_rt.dll":
            print(os.path.join(root, f))

numpy.random.seed(0)

numpy.set_printoptions(threshold=7)

from embeddings import embeddings

topk = 7

print("Appending data...")

db = embeddings.open(dim=64, mode="a+")

data = [(uuid.uuid4().bytes, numpy.random.rand(64).astype(numpy.float32)) for i in range(topk * 3)]

for i, vec in enumerate(data):
    db.append(vec[0], vec[1])
    if i < topk:
        print(f"id: {vec[0]}, size: {len(vec[1])}")
        print(f"{vec[1]}")

db.flush()

print("Done")

cur = db.cursor()

cur.reset()
cc = 0
while True:
    rec = cur.read()
    if rec is None:
        break
    assert rec[0] == data[cc][0]
    assert numpy.array_equal(numpy.frombuffer(rec[1], dtype=numpy.float32), numpy.array(data[cc][1]))
    if cc < topk:
        print(f"id: {rec[0]}, size: {len(rec[1])}")
    cur.update(rec[0], rec[1])
    cc += 1

cur.close()

print("\nPass\n")

query = data[0]

print(f"query: {query[0]}, size: {len(query[1])}")
print(f"{query[1]}")
print()

print(numpy.frombuffer(query[1], dtype=numpy.float32))

results = db.search(query[1], topk=topk)

for id, score in results:
    print("id:", id, "score:", score)

print(results[0][0])
print(query[0])

assert results[0][0] == query[0]
assert results[0][1] == 1.0

print("\nPass\n")