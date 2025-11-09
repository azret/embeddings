# https://fasttext.cc/docs/en/english-vectors.html

import io, numpy

def fetchfasttext(path, take=1000):
    with io.open(path, 'r', encoding='utf-8', newline='\n', errors='ignore') as fin:
        n, d = map(int, fin.readline().split())
        for i, line in enumerate(fin):
            if i >= take:
                break
            tokens = line.rstrip().split()
            word = tokens[0]
            vec = numpy.fromstring(' '.join(tokens[1:]), sep=' ', dtype=numpy.float32)
            yield word, vec

import embeddings

db = embeddings.open("./examples/300d-1M-subword.300", dim=300, mode="a+")

query = None

data = fetchfasttext("./examples/300d-1M-subword.vec", 10000)
for word, vec in data:
    if word == "mother": # we currently don't have lookups in embeddings db, so just use one of the vectors as query
        query = vec
    uiid = word.encode("utf-8")[:16].ljust(16, b'\0')
    db.append(uiid, vec)
    print(word)
    pass

db.flush()

results = db.search(query, topk=7)

for id, score in results:
    print("id:", id, "score:", score)

db.close()
