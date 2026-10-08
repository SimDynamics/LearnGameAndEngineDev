"""Export Misaki's selected original US/GB BART models, without downloading anything.

Inputs must be the pinned, verified PeterReid source models. Outputs are two full FP32
ONNX graphs per dialect, with a checked manifest and a local installation ZIP. Both
graphs use standard opset 17 and no key/value cache. The greedy loop is checked against
the original transformers 4.51.2 generate() call, including its default length/EOS.

Build dependencies: torch==2.6.0, transformers==4.51.2, onnx==1.17.0,
onnxruntime==1.22.0, safetensors==0.5.3, numpy==1.26.4. Python 3.11/3.12.
This export wrapper is project code; the source model cards declare Apache-2.0.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import zipfile

# Keep transformers entirely local; no missing file may trigger a network request.
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"

import numpy as np
import onnx
import onnxruntime as ort
import torch
import transformers
from transformers import BartForConditionalGeneration

MISAKI_REVISION = "fba1236595f2d2bf21d414ba6e57d25256afada3"
SOURCES = {
    "us": {
        "revision": "a5631b285d18d59483c32c0c3379cb9fac924f4b",
        "sha256": "dc4a02e62d4fcb4bb4097ecf00db89b8e1a12a549a52ab6adfbba220b80a55c5",
        "config_sha256": "8deb3537fb29c63cd9f20d75515ae06e4c92f1b6db0703a2d45bca95b33a53a4",
    },
    "gb": {
        "revision": "d8357d5067fa26a5c34134d6bbcf4bbf000c0ac8",
        "sha256": "4994f474bb6f4584076a4e98189caaec11aa3f773a0d82d5bc263a03dd07e703",
        "config_sha256": "e4f248e6af0cfb6cb54aea6ff0168d16b6f3cdabed438c487cde4ab0815d1bac",
    },
}
GENERATION_SHA256 = "79a11240b01c6d34cab0b122b63a2b2dd5938c4d37d1530e14144d27e8ba6272"
WORDS = ["hello", "world", "outofdictionary", "foobar", "graphical", "Graphi", "LearnGraphicsProgramming",
         "acknowledgement", "artefact", "algebraically", "quaternion", "polynomial", "eigenvalue",
         "antiestablishmentarianism", "supercalifragilisticexpialidocious", "colour", "color", "rhythm",
         "rotational", "orthonormality", "stereoscopic", "A", "I", "x", "xyz", "PeterReid", "HuggingFace",
         "café", "naïve", "can't", "uncharacteristically", "hyperparameterization", "onomatopoeia"]


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


class Encoder(torch.nn.Module):
    def __init__(self, model):
        super().__init__()
        self.encoder = model.model.encoder

    def forward(self, input_ids, attention_mask):
        return self.encoder(input_ids=input_ids, attention_mask=attention_mask, return_dict=False)[0]


class Decoder(torch.nn.Module):
    def __init__(self, model):
        super().__init__()
        self.decoder = model.model.decoder
        self.lm_head = model.lm_head
        self.register_buffer("final_logits_bias", model.final_logits_bias)

    def forward(self, decoder_input_ids, encoder_hidden_states, encoder_attention_mask):
        hidden = self.decoder(input_ids=decoder_input_ids, encoder_hidden_states=encoder_hidden_states,
                              encoder_attention_mask=encoder_attention_mask, use_cache=False, return_dict=False)[0]
        return (self.lm_head(hidden) + self.final_logits_bias)[:, -1, :]


def encode(word, config):
    if not 1 <= len(word) <= 62:
        raise ValueError("Input must fit the original 64-token encoder")
    chars = {symbol: index for index, symbol in enumerate(config.grapheme_chars)}
    return np.array([[1] + [chars.get(symbol, 3) for symbol in word] + [2]], dtype=np.int64)


def generate_onnx(ids, encoder, decoder):
    attention = np.ones_like(ids)
    hidden = encoder.run(["hidden_states"], {"input_ids": ids, "attention_mask": attention})[0]
    output = [1]
    while len(output) < 21:
        logits = decoder.run(["logits"], {
            "decoder_input_ids": np.array([output], dtype=np.int64),
            "encoder_hidden_states": hidden,
            "encoder_attention_mask": attention,
        })[0]
        if logits.shape != (1, 63) or not np.all(np.isfinite(logits)):
            raise AssertionError("Invalid ONNX decoder output")
        token = 2 if len(output) == 20 else int(np.argmax(logits[0]))
        output.append(token)
        if token == 2:
            break
    return output


def export_variant(source_root, output_root, variant):
    source = SOURCES[variant]
    directory = source_root / variant
    for name, expected in [("model.safetensors", source["sha256"]), ("config.json", source["config_sha256"]),
                           ("generation_config.json", GENERATION_SHA256)]:
        if sha256(directory / name) != expected:
            raise ValueError(f"Source integrity failed: {variant}/{name}")
    model = BartForConditionalGeneration.from_pretrained(directory, local_files_only=True, attn_implementation="eager")
    model.float().eval()
    encoder = Encoder(model).eval()
    decoder = Decoder(model).eval()
    ids = torch.from_numpy(encode("misaki", model.config))
    attention = torch.ones_like(ids)
    with torch.no_grad():
        hidden = encoder(ids, attention)
    encoder_file = output_root / f"en_{variant}_bart_encoder.onnx"
    decoder_file = output_root / f"en_{variant}_bart_decoder.onnx"
    torch.onnx.export(encoder, (ids, attention), str(encoder_file), opset_version=17, dynamo=False,
                      input_names=["input_ids", "attention_mask"], output_names=["hidden_states"],
                      dynamic_axes={"input_ids": {1: "source_length"}, "attention_mask": {1: "source_length"},
                                    "hidden_states": {1: "source_length"}})
    torch.onnx.export(decoder, (torch.tensor([[1, 7, 8]], dtype=torch.int64), hidden, attention), str(decoder_file),
                      opset_version=17, dynamo=False,
                      input_names=["decoder_input_ids", "encoder_hidden_states", "encoder_attention_mask"], output_names=["logits"],
                      dynamic_axes={"decoder_input_ids": {1: "target_length"}, "encoder_hidden_states": {1: "source_length"},
                                    "encoder_attention_mask": {1: "source_length"}})
    for file in (encoder_file, decoder_file):
        onnx.checker.check_model(str(file), full_check=True)
    options = ort.SessionOptions()
    options.intra_op_num_threads = 2
    options.inter_op_num_threads = 1
    enc_ort = ort.InferenceSession(str(encoder_file), options, providers=["CPUExecutionProvider"])
    dec_ort = ort.InferenceSession(str(decoder_file), options, providers=["CPUExecutionProvider"])
    results = []
    # Include adversarial lengths and unseen words; exact generated IDs matter more than
    # plausible IPA alone, and exercise dynamic dimensions in both graphs.
    for word in WORDS + ["a" * count for count in (1, 2, 17, 31, 62)]:
        inputs = encode(word, model.config)
        with torch.no_grad():
            reference = model.generate(input_ids=torch.from_numpy(inputs))[0].tolist()
        actual = generate_onnx(inputs, enc_ort, dec_ort)
        if actual != reference:
            raise AssertionError(f"{variant} {word}: ONNX {actual} != original {reference}")
        phonemes = "".join(model.config.phoneme_chars[index] for index in actual if 3 < index < len(model.config.phoneme_chars))
        results.append({"word": word, "ids": actual, "phonemes": phonemes})
    def artifact(file):
        return {"name": file.name, "bytes": file.stat().st_size, "sha256": sha256(file)}
    manifest = {
        "format": 1, "license": "Apache-2.0", "misaki_revision": MISAKI_REVISION,
        "source": f"PeterReid/graphemes_to_phonemes_en_{variant}", "source_revision": source["revision"],
        "source_sha256": source["sha256"], "transformers": transformers.__version__, "torch": torch.__version__,
        "onnx": onnx.__version__, "opset": 17, "maximum_generated_tokens": 21,
        "maximum_input_tokens": 64, "vocab_size": model.config.vocab_size,
        "grapheme_chars": model.config.grapheme_chars, "phoneme_chars": model.config.phoneme_chars,
        "encoder": artifact(encoder_file), "decoder": artifact(decoder_file),
    }
    manifest_file = output_root / f"en_{variant}_bart.json"
    manifest_file.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (output_root / f"en_{variant}_bart_equivalence.json").write_text(
        json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"variant": variant, "exact_equivalence_cases": len(results), "artifacts": [
        artifact(encoder_file), artifact(decoder_file), artifact(manifest_file)]}, ensure_ascii=False))
    return [encoder_file, decoder_file, manifest_file]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if transformers.__version__ != "4.51.2" or torch.__version__.split("+")[0] != "2.6.0":
        raise RuntimeError("Use the pinned export environment; decoding/export behavior is version-sensitive")
    torch.set_num_threads(2)
    torch.manual_seed(0)
    args.output.mkdir(parents=True, exist_ok=True)
    files = []
    for variant in ("us", "gb"):
        files.extend(export_variant(args.source, args.output, variant))
    with zipfile.ZipFile(args.output / "misaki-bart-install.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for file in files:
            archive.write(file, file.name)


if __name__ == "__main__":
    main()
