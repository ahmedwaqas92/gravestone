#!/usr/bin/env python3
"""Builds data/catalogue.tsv from the Ollama library.

The library page gives the model names and pull counts. The tags page
gives every tag. The registry gives, per tag, the size of the weights
blob and the config blob that names the quantisation. Rows are deduped
by weights digest, since latest and the bare size tag usually point at
the same file.
"""
import concurrent.futures as fut
import json
import re
import sys
import urllib.request

TOP_MODELS = 160
MAX_ROWS = 2000
QUANT_IN_TAG = re.compile(
    r'-(i?q[0-9][a-z0-9_]*|fp16|bf16|f16|f32|nvfp4|mxfp[0-9]*)$', re.I)

def get(url, accept=None, tries=3):
    for attempt in range(tries):
        try:
            req = urllib.request.Request(url, headers={
                "User-Agent": "gravestone-catalogue/1.0",
                **({"Accept": accept} if accept else {})})
            with urllib.request.urlopen(req, timeout=25) as r:
                return r.read().decode("utf-8", "replace")
        except Exception:
            if attempt == tries - 1:
                return None
    return None

def parse_pulls(text):
    m = re.match(r'([0-9][0-9.,]*)([KMB]?)', text)
    if not m:
        return 0
    n = float(m.group(1).replace(",", ""))
    return int(n * {"": 1, "K": 1e3, "M": 1e6, "B": 1e9}[m.group(2)])

def library_models():
    html = get("https://ollama.com/library?sort=popular")
    if html is None:
        sys.exit("the library page would not load")
    models = []
    for block in html.split('href="/library/')[1:]:
        name = block.split('"', 1)[0]
        pm = re.search(r'<span >([0-9][0-9.,]*[KMB]?)</span>\s*'
                       r'<span[^>]*>&nbsp;Pulls', block)
        pulls = parse_pulls(pm.group(1)) if pm else 0
        if name and not any(n == name for n, _ in models):
            models.append((name, pulls))
    return models[:TOP_MODELS]

def tags_of(model):
    html = get("https://ollama.com/library/%s/tags" % model)
    if html is None:
        return []
    tags = sorted(set(re.findall(re.escape(model) + r':([A-Za-z0-9._-]+)',
                                 html)))
    return [t for t in tags
            if t != "latest" and "mlx" not in t and "cloud" not in t]

def manifest_of(model, tag):
    text = get("https://registry.ollama.ai/v2/library/%s/manifests/%s"
               % (model, tag),
               accept="application/vnd.docker.distribution.manifest.v2+json")
    if text is None:
        return None
    try:
        d = json.loads(text)
    except ValueError:
        return None
    weights = None
    for layer in d.get("layers", []):
        if layer.get("mediaType", "").endswith("image.model"):
            weights = layer
    if weights is None:
        return None
    return {"digest": weights["digest"], "bytes": int(weights["size"]),
            "config": d.get("config", {}).get("digest")}

def quant_of(model, tag, config_digest, cache):
    m = QUANT_IN_TAG.search(tag)
    if m:
        return m.group(1).upper().replace("FP16", "F16")
    if config_digest in cache:
        return cache[config_digest]
    text = get("https://registry.ollama.ai/v2/library/%s/blobs/%s"
               % (model, config_digest))
    quant = "unknown"
    if text is not None:
        try:
            quant = json.loads(text).get("file_type", "unknown") or "unknown"
        except ValueError:
            pass
    cache[config_digest] = quant
    return quant

def rows_for(model, pulls):
    rows = {}
    cache = {}
    for tag in tags_of(model):
        info = manifest_of(model, tag)
        if info is None or info["bytes"] <= 0:
            continue
        quant = quant_of(model, tag, info["config"], cache)
        key = info["digest"]
        row = ("library/%s" % model, "%s:%s" % (model, tag), quant,
               info["bytes"], pulls)
        # aliases share a digest, and the shortest tag is the one people
        # type, so it is the one that survives
        if key not in rows or len(tag) < len(rows[key][0]):
            rows[key] = (tag, row)
    return [r for _, r in rows.values()]

def main():
    models = library_models()
    print("models on the library page: %d" % len(models), file=sys.stderr)
    all_rows = []
    with fut.ThreadPoolExecutor(max_workers=8) as pool:
        jobs = {pool.submit(rows_for, n, p): n for n, p in models}
        done = 0
        for job in fut.as_completed(jobs):
            all_rows.extend(job.result())
            done += 1
            if done % 10 == 0:
                print("  %d models read, %d rows" % (done, len(all_rows)),
                      file=sys.stderr)
    # A family with dozens of tags would crowd whole models out of the
    # budget, so each model keeps at most 24 rows, smallest first, since
    # small machines are who the fit list serves.
    by_model = {}
    for r in all_rows:
        by_model.setdefault(r[0], []).append(r)
    all_rows = []
    for rows in by_model.values():
        # bare tags are the names people type, so they always survive,
        # and explicit variants fill the rest smallest first
        bare = [r for r in rows if "-" not in r[1].split(":", 1)[1]]
        rest = sorted([r for r in rows if r not in bare],
                      key=lambda r: r[3])
        all_rows.extend(bare + rest[:max(0, 24 - len(bare))])
    all_rows.sort(key=lambda r: (-r[4], r[1]))
    all_rows = all_rows[:MAX_ROWS]
    # every fetched row lands in a side file too, so a different budget
    # can be cut without fetching again
    side = open("data/catalogue-full.tsv", "w")
    for r in sorted(by_model):
        for row in sorted(by_model[r], key=lambda x: x[3]):
            side.write("%s\t%s\t%s\t%d\t%d\n" % row)
    side.close()
    out = open("data/catalogue.tsv", "w")
    out.write("# gravestone model catalogue\n")
    out.write("# repository\tfile\tquantisation\tbytes\tdownloads\n")
    for r in all_rows:
        out.write("%s\t%s\t%s\t%d\t%d\n" % r)
    out.close()
    print("wrote %d rows" % len(all_rows), file=sys.stderr)

main()
