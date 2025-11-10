import os
import requests
import time
import numpy
import embeddings

from pathlib import Path

BASE_URL = "https://api.openai.com"

def fetchembeddings(docs) -> float:
    OPENAI_API_KEY = os.getenv("OPENAI_API_KEY", "")
    url = f"{BASE_URL}/v1/embeddings"
    payload = {
        "model": "text-embedding-3-small",
        "dimensions": 384,
        "input": docs,
    }
    headers = {
        "Authorization": f"Bearer {OPENAI_API_KEY}",
        "Content-Type": "application/json",
    }
    res = requests.post(url, json=payload, headers=headers)
    data = res.json()
    return [
            d["embedding"] for d in data["data"]
    ]

def doctext(doc):
    return f"**Subject**:\r\n{doc["subject"]}\r\n**Body**:\r\n{doc["body"]}"

def batchsize():
    limit = 128 if BASE_URL.endswith("127.0.0.1:8000") or BASE_URL == "https://api.openai.com" else 10
    return limit

def fetchbatch(a, offset: int, limit: int = 128):
    if limit <= 0:
        raise ValueError("'limit' must be positive")
    if offset < 0:
        raise ValueError("offset must be non-negative")
    if limit > 128:
        raise ValueError("'limit' must be at most 128")
    if offset >= len(a):
        return []
    b = a[offset:offset+limit]
    return b

def loaddat():
    import yaml
    print(f"Loading {str(Path(__file__).with_suffix(".yaml"))}")
    with open(str(Path(__file__).with_suffix(".yaml")), "r", encoding="utf-8") as f:
        a = yaml.safe_load(f)[:1000]
    offset = 0
    while True:
        b = fetchbatch(a, offset, limit=batchsize())
        if not b or len(b) == 0:
            break
        print(f"Processing batch[{offset}:{offset+len(b)}]...")
        docs = [doctext(t) for t in b]
        offset += len(docs)
        embeddings = fetchembeddings(docs)
        assert len(embeddings) == len(docs)
        for i in range(len(b)):
            b[i]["embedding"] = embeddings[i]
        """ small sleep to respect rate limits """
        time.sleep(0.3)
    dat = dict()
    for t in a:
        assert t["id"] not in dat
        dat[t["id"]] = t
    return dat

def buildvecs(dat: dict):
    print(f"Building vector db...")
    db = embeddings.open(":temp:", dim=384, mode="a++")
    CC = 0
    for id in dat:
        uiid = id.encode("utf-8")[:16].ljust(16, b'\0')
        t = dat[id]
        blob = numpy.array(t["embedding"], dtype=numpy.float32)
        db.append(uiid, blob)
        if CC % 100 == 0:
            print(f"  Indexed {CC} documents...")
        CC += 1
    db.flush()
    return db

if __name__ == "__main__":
    dat = loaddat()
    db = buildvecs(dat)
    while True:
        try:
            queryString = input(f"\x1b[38;2;{66};{244};{66}m{"Model"}\x1b[0m>").strip()
            if queryString == "cls":
                os.system('cls' if os.name == 'nt' else 'clear')
                print("\x1b[H\x1b[2J\x1b[3J", end="")
                continue
            if queryString:
                print(f"\x1b[38;2;{255};{255};{255}m")
                try:
                    q = numpy.squeeze(numpy.array(
                        fetchembeddings(queryString),
                        dtype=numpy.float32
                    ))
                    topk = db.search(q, topk=7)
                    for uiid, score in topk:
                        id = uiid.rstrip(b'\0').decode("utf-8", errors="ignore")
                        t = dat[id]
                        print("============")
                        print(f"cosine: {score}")
                        print(f"id: {t["id"]}")
                        print(f"account: {t["account"]}")
                        print(f"channel: {t["channel"]}")
                        print(f"priority: {t["priority"]}")
                        print(f"subject: {t["subject"]}")
                        print(f"body: {t["body"]}")
                        print("============")
                        print()
                finally:
                    print("\x1b[0m")
        except KeyboardInterrupt:
            exit()
