# embeddings.c

Single file vector store (Windows Only).

### Examples

### Installation

### Building from source code (Windows Only)

Clone the repository:

```bash
git clone
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
