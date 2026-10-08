"""Export the approved spaCy 3.8.0 tok2vec/tagger to a faithful CPU ONNX graph.

No network calls, new training, quantization, alternate POS engine, or model download.
Run using the explicitly prepared spaCy/ONNX environment and a verified local wheel.
The output is a local conversion, not an upstream ONNX release. Token hash buckets
are prepared by the companion Android frontend using spaCy/Thinc's exact hashes.

Verified conversion environment: Python 3.12, spaCy 3.8.7, Thinc 8.3.11,
numpy 1.26.4, onnx 1.17.0, onnxruntime 1.22.0, typer-slim 0.15.2, click 8.1.8.
The CLI dependencies are conversion-only and are not distributed in the Android APK.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import tempfile
import zipfile
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
import spacy
import thinc
from onnx import TensorProto, helper, numpy_helper
from spacy import attrs, symbols
from spacy.lang.norm_exceptions import BASE_NORMS

WHEEL_SHA256 = "1932429db727d4bff3deed6b34cfc05df17794f4a52eeb26cf8928f7c1a0fb85"
WHEEL_BYTES = 12_806_118
FEATURES = [attrs.NORM, attrs.PREFIX, attrs.SUFFIX, attrs.SHAPE, attrs.SPACY, attrs.IS_SPACE]


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def parameter_nodes(model, name):
    result, seen = [], set()

    def visit(node):
        if node.id in seen:
            return
        seen.add(node.id)
        if node.name == name:
            result.append(node)
        for child in node.layers:
            visit(child)

    visit(model)
    return result


class Graph:
    def __init__(self):
        self.nodes, self.initializers = [], []
        self.count = 0

    def constant(self, name, value):
        self.initializers.append(numpy_helper.from_array(np.asarray(value), name))
        return name

    def op(self, kind, inputs, **attributes):
        self.count += 1
        output = f"{kind}_{self.count}"
        self.nodes.append(helper.make_node(kind, inputs, [output], **attributes))
        return output

    def maxout_normalized(self, value, maxout, norm, name):
        weights = maxout.get_param("W")
        output_width, pieces, input_width = weights.shape
        value = self.op("MatMul", [value, self.constant(name + "_W", weights.reshape(output_width * pieces, input_width).T.copy())])
        value = self.op("Add", [value, self.constant(name + "_b", maxout.get_param("b").reshape(-1))])
        value = self.op("Reshape", [value, self.constant(name + "_shape", np.array([-1, output_width, pieces], dtype=np.int64))])
        value = self.op("ReduceMax", [value], axes=[2], keepdims=0)
        mean = self.op("ReduceMean", [value], axes=[1], keepdims=1)
        distance = self.op("Sub", [value, mean])
        square = self.op("Mul", [distance, distance])
        variance = self.op("ReduceMean", [square], axes=[1], keepdims=1)
        variance = self.op("Add", [variance, self.constant(name + "_eps", np.array(1e-8, dtype=np.float32))])
        denominator = self.op("Sqrt", [variance])
        value = self.op("Div", [distance, denominator])
        value = self.op("Mul", [value, self.constant(name + "_G", norm.get_param("G"))])
        return self.op("Add", [value, self.constant(name + "_shift", norm.get_param("b"))])


def export_model(nlp, destination):
    tok2vec = nlp.get_pipe("tok2vec").model
    embed = tok2vec.get_ref("embed")
    encode = tok2vec.get_ref("encode")
    embeddings = parameter_nodes(embed, "hashembed")
    embed_maxout = parameter_nodes(embed, "maxout")
    embed_norm = parameter_nodes(embed, "layernorm")
    encoder_maxout = parameter_nodes(encode, "maxout")
    encoder_norm = parameter_nodes(encode, "layernorm")
    classifier = parameter_nodes(nlp.get_pipe("tagger").model, "softmax")
    assert len(embeddings) == 6 and len(embed_maxout) == len(embed_norm) == 1
    assert len(encoder_maxout) == len(encoder_norm) == 4 and len(classifier) == 1
    assert [e.get_param("E").shape for e in embeddings] == [(5000, 96), (1000, 96), (2500, 96), (2500, 96), (50, 96), (50, 96)]
    graph = Graph()
    vectors = []
    for index, embedding in enumerate(embeddings):
        keys = graph.op("Gather", ["keys", graph.constant(f"feature_{index}", np.array(index, dtype=np.int64))], axis=1)
        gathered = graph.op("Gather", [graph.constant(f"E_{index}", embedding.get_param("E")), keys], axis=0)
        vectors.append(graph.op("ReduceSum", [gathered, graph.constant(f"reduce_{index}", np.array([1], dtype=np.int64))], keepdims=0))
    value = graph.op("Concat", vectors, axis=1)
    value = graph.maxout_normalized(value, embed_maxout[0], embed_norm[0], "embed")
    # spaCy with_array(..., pad=window_size*depth) pads the sequence once before
    # all residual blocks. Padded rows receive the same learned updates, so replacing
    # them by zero afresh at each layer would alter real sentence-edge logits.
    padding = graph.constant("encoder_padding", np.zeros((4, 96), dtype=np.float32))
    value = graph.op("Concat", [padding, value, padding], axis=0)
    zero = graph.constant("window_zero", np.zeros((1, 96), dtype=np.float32))
    for index, (maxout, norm) in enumerate(zip(encoder_maxout, encoder_norm)):
        previous = graph.op("Slice", [value, graph.constant(f"prev_start_{index}", np.array([0], dtype=np.int64)), graph.constant(f"prev_end_{index}", np.array([-1], dtype=np.int64)), graph.constant(f"prev_axis_{index}", np.array([0], dtype=np.int64))])
        previous = graph.op("Concat", [zero, previous], axis=0)
        following = graph.op("Slice", [value, graph.constant(f"next_start_{index}", np.array([1], dtype=np.int64)), graph.constant(f"next_end_{index}", np.array([np.iinfo(np.int64).max], dtype=np.int64)), graph.constant(f"next_axis_{index}", np.array([0], dtype=np.int64))])
        following = graph.op("Concat", [following, zero], axis=0)
        window = graph.op("Concat", [previous, value, following], axis=1)
        update = graph.maxout_normalized(window, maxout, norm, f"cnn_{index}")
        value = graph.op("Add", [value, update])
    value = graph.op("Slice", [value, graph.constant("unpad_start", np.array([4], dtype=np.int64)), graph.constant("unpad_end", np.array([-4], dtype=np.int64)), graph.constant("unpad_axis", np.array([0], dtype=np.int64))])
    linear = classifier[0]
    assert linear.attrs.get("softmax_normalize") is False
    assert linear.get_param("W").shape == (50, 96)
    value = graph.op("MatMul", [value, graph.constant("tagger_W", linear.get_param("W").T.copy())])
    value = graph.op("Add", [value, graph.constant("tagger_b", linear.get_param("b"))])
    graph.nodes.append(helper.make_node("Identity", [value], ["logits"]))
    model = helper.make_model(helper.make_graph(graph.nodes, "spaCy_en_core_web_sm_3_8_0_exact_tagger", [helper.make_tensor_value_info("keys", TensorProto.INT64, ["tokens", 6, 4])], [helper.make_tensor_value_info("logits", TensorProto.FLOAT, ["tokens", 50])], graph.initializers), opset_imports=[helper.make_opsetid("", 14)], producer_name="LearnGraphicsProgramming spaCy/Thinc faithful conversion")
    model.ir_version = 9
    onnx.checker.check_model(model)
    onnx.save(model, destination)
    return embeddings


def pattern(callable_):
    if callable_ is None:
        return None
    compiled = callable_.__self__
    return {"source": compiled.pattern, "ignoreCase": bool(compiled.flags & re.IGNORECASE), "multiline": bool(compiled.flags & re.MULTILINE), "dotAll": bool(compiled.flags & re.DOTALL)}


def unicode_tables():
    lowercase, ranges, whitespace = {}, [], []
    current_kind, beginning = None, 0
    for codepoint in range(0x110000):
        char = chr(codepoint)
        lower = char.lower()
        if lower != char:
            lowercase[str(codepoint)] = lower
        kind = "X" if char.isalpha() and char.isupper() else "x" if char.isalpha() else "d" if char.isdigit() else ""
        if kind != current_kind:
            if current_kind:
                ranges.append([beginning, codepoint - 1, current_kind])
            beginning, current_kind = codepoint, kind
        if char.isspace():
            whitespace.append(codepoint)
    if current_kind:
        ranges.append([beginning, 0x10ffff, current_kind])
    return lowercase, ranges, whitespace


def export_frontend(nlp, embeddings, destination):
    tokenizer = nlp.tokenizer
    lower, shape_ranges, whitespace = unicode_tables()
    rules = {word: [{"text": part[attrs.ORTH], "norm": part.get(attrs.NORM)} for part in parts] for word, parts in tokenizer.rules.items()}
    norm_lookup = nlp.vocab.lookups.get_table("lexeme_norm", {})
    data = {
        "format": 1, "pipeline": "en_core_web_sm", "pipelineVersion": "3.8.0",
        "spacyVersion": spacy.__version__, "thincVersion": thinc.__version__,
        "sourceWheelSha256": WHEEL_SHA256,
        "graphSha256": sha256(destination.with_suffix(".onnx")),
        "graphBytes": destination.with_suffix(".onnx").stat().st_size,
        "labels": list(nlp.get_pipe("tagger").labels),
        "features": ["NORM", "PREFIX", "SUFFIX", "SHAPE", "SPACY", "IS_SPACE"],
        "seeds": [int(embedding.attrs["seed"]) for embedding in embeddings],
        "rows": [embedding.get_dim("nV") for embedding in embeddings],
        "symbols": dict(symbols.IDS), "lowercase": lower,
        "shapeRanges": shape_ranges, "whitespace": whitespace,
        "lexemeNorm": {str(key): value for key, value in norm_lookup.items()},
        "baseNorms": {word: nlp.vocab.lex_attr_getters[attrs.NORM](word) for word in BASE_NORMS},
        "rules": rules, "prefix": pattern(tokenizer.prefix_search),
        "suffix": pattern(tokenizer.suffix_search), "infix": pattern(tokenizer.infix_finditer),
        "tokenMatch": pattern(tokenizer.token_match), "urlMatch": pattern(tokenizer.url_match),
    }
    destination.write_text(json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(",", ":")), encoding="utf-8")


def buckets(doc, embeddings):
    features = doc.to_array(FEATURES)
    return np.stack([embedding.ops.hash(np.ascontiguousarray(features[:, column]), int(embedding.attrs["seed"])) % embedding.get_dim("nV") for column, embedding in enumerate(embeddings)], axis=1).astype(np.int64)


def verify_export(nlp, embeddings, model_path, golden_path):
    session = ort.InferenceSession(str(model_path), providers=["CPUExecutionProvider"])
    # Each expected output comes from the actual authorized pipeline, including ambiguous tags,
    # apostrophes, symbols, retained whitespace and context across the full sentence.
    sentences = [
        "Equivalent fractions represent the same quantity. Three quarters equals six eighths.",
        "The angle is 45 degrees. The square root of 25 equals 5.",
        "I read books every day. Yesterday I read a book.",
        "They will read it. Yesterday they read it.",
        "They record a record. Please present the present.",
        "The bass swims near the bass instrument.",
        "Don't stop. It's John's well-known example, isn't it?",
        "Dr. Smith paid $12.50 at 3:15 p.m. on Jan. 5th.",
        "First  second\tthird\nFourth. Visit https://example.com/a-b.",
        "A résumé contains café and naïve: ² ß İ 𐐀.",
        "Read", " ", "(New York) can't won't I'm they've.",
    ]
    golden, maximum_error = [], 0.0
    labels = list(nlp.get_pipe("tagger").labels)
    for text in sentences:
        doc = nlp(text)
        keys = buckets(doc, embeddings)
        actual = session.run(["logits"], {"keys": keys})[0]
        vectors = nlp.get_pipe("tok2vec").model.predict([doc])[0]
        linear = parameter_nodes(nlp.get_pipe("tagger").model, "softmax")[0]
        expected = vectors @ linear.get_param("W").T + linear.get_param("b")
        maximum_error = max(maximum_error, float(np.max(np.abs(actual - expected))))
        np.testing.assert_allclose(actual, expected, rtol=3e-5, atol=3e-5)
        actual_tags = [labels[index] for index in actual.argmax(axis=1)]
        assert actual_tags == [token.tag_ for token in doc], (text, actual_tags, [token.tag_ for token in doc])
        golden.append({"text": text, "tokens": [{"text": token.text, "whitespace": token.whitespace_, "tag": token.tag_, "norm": token.norm_, "features": [str(value) for value in row], "keys": key.tolist()} for token, row, key in zip(doc, doc.to_array(FEATURES), keys)]})
    golden_path.write_text(json.dumps(golden, ensure_ascii=False, indent=2), encoding="utf-8")
    return {"sentences": len(sentences), "maxLogitAbsoluteError": maximum_error, "tagsIdentical": True}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--wheel", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    assert args.wheel.stat().st_size == WHEEL_BYTES and sha256(args.wheel) == WHEEL_SHA256
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="learngraph-spacy-export-") as temporary:
        root = Path(temporary)
        prefix = "en_core_web_sm/en_core_web_sm-3.8.0/"
        with zipfile.ZipFile(args.wheel) as archive:
            for name in archive.namelist():
                if not name.startswith(prefix):
                    continue
                relative = name[len(prefix):]
                if not relative or relative.split("/")[0] in {"ner", "parser", "senter", "attribute_ruler", "lemmatizer"}:
                    continue
                target = (root / relative).resolve()
                assert target.is_relative_to(root.resolve())
                target.parent.mkdir(parents=True, exist_ok=True)
                if not name.endswith("/"):
                    target.write_bytes(archive.read(name))
        nlp = spacy.load(root, exclude=["parser", "senter", "attribute_ruler", "lemmatizer", "ner"])
        assert nlp.meta["version"] == "3.8.0" and nlp.pipe_names == ["tok2vec", "tagger"]
        model_path = args.output / "spacy_pos.onnx"
        frontend_path = args.output / "spacy_pos.json"
        embeddings = export_model(nlp, model_path)
        export_frontend(nlp, embeddings, frontend_path)
        verification = verify_export(nlp, embeddings, model_path, args.output / "spacy_pos_goldens.json")
        license_path = root / "LICENSE"
        if license_path.is_file():
            (args.output / "spacy_model_license.txt").write_bytes(license_path.read_bytes())
        manifest = {"sourceUrl": "https://github.com/explosion/spacy-models/releases/download/en_core_web_sm-3.8.0/en_core_web_sm-3.8.0-py3-none-any.whl", "sourceBytes": WHEEL_BYTES, "sourceSha256": WHEEL_SHA256, "sourceLicense": "MIT", "conversion": "tok2vec + tagger, original float32 weights, ONNX opset 14", "verification": verification, "artifacts": [{"fileName": path.name, "bytes": path.stat().st_size, "sha256": sha256(path)} for path in [model_path, frontend_path]]}
        (args.output / "spacy_pos_manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
