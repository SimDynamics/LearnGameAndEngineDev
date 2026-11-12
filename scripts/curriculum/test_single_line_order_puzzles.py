"""All Spanish programming order_code cards are exactly one physical line."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
LESSONS = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos"
CASES = json.loads(r'''[["lesson_declaraciones_y_firmas.json",4,8,"dias_completos_definicion_anterior_orden",3,9,"","6",2],["lesson_hacer_explicito_el_orden_de_las_operaciones.json",3,19,"registro_orden_propio_orden",4,19,"2 7\n","9:27",3],["lesson_nombrar_calculos_y_llamar.json",5,8,"construccion_lotes_y_sueltas_orden",4,12,"2 4\n","Fichas: 10\n",2]]''')


def program(block):
    return "\n".join(block["fragments"][i] for i in block["correctOrder"]) + "\n"


class RealFunctionOrderCards(unittest.TestCase):
    def test_all_current_spanish_programming_order_cards_are_single_line(self):
        root = ROOT / "app/src/main/assets/content/es/programming"
        violations = []
        for path in sorted(root.glob("**/lesson_*.json")):
            lesson = json.loads(path.read_text(encoding="utf-8-sig"))
            for p, page in enumerate(lesson["pages"]):
                for b, block in enumerate(page.get("blocks", [])):
                    if block.get("type") != "order_code":
                        continue
                    for index, line in enumerate(block["fragments"]):
                        if "\n" in line or "\r" in line:
                            violations.append(f"{path.relative_to(root)} p{p+1} b{b+1} tarjeta{index}")
        self.assertEqual([], violations)

    def test_three_function_puzzles_preserve_original_answers_and_state(self):
        import hashlib
        for name, p, b, identity, old_count, new_count, stdin, expected, sep in CASES:
            with self.subTest(lesson=name):
                published = json.loads((LESSONS / name).read_text(encoding="utf-8"))
                state.validate(published)
                # Rewind later, independent source-spacing revisions before
                # reconstructing the two earlier order-card editorial proofs.
                # These proofs are no longer necessarily the last two entries.
                after = deepcopy(published)
                revisions = after["stateLayout"].get("sourceFormatRevisions", [])
                prose_suffixes = {}
                if name == "lesson_hacer_explicito_el_orden_de_las_operaciones.json":
                    from test_orden_operaciones_format_batch import SUFFIXES
                    prose_suffixes = SUFFIXES
                # Reconstruct the published state before both independent prose
                # and source-spacing edits. Neither may alter the order-card
                # answer or its earlier two editorial proofs.
                if revisions or prose_suffixes:
                    from code_format_compat import require_source_format
                    meta = after["stateLayout"]
                    appended = [item["id"] for item in revisions] + list(prose_suffixes)
                    self.assertEqual(
                        appended,
                        [item["id"] for item in meta["editorial"][-len(appended):]],
                    )
                    coordinates = {
                        identity: (page_index, block_index)
                        for page_index, page in enumerate(meta["current"])
                        for block_index, identity in enumerate(page["blocks"])
                    }
                    for item in revisions:
                        page_index, block_index = coordinates[item["id"]]
                        formatted = after["pages"][page_index]["blocks"][block_index]
                        self.assertTrue(require_source_format(item["block"], formatted))
                        after["pages"][page_index]["blocks"][block_index] = deepcopy(item["block"])
                    for prose_id, suffix in prose_suffixes.items():
                        page_index, block_index = coordinates[prose_id]
                        block = after["pages"][page_index]["blocks"][block_index]
                        self.assertTrue(block["text"].endswith(suffix), prose_id)
                        block["text"] = block["text"][:-len(suffix)]
                        descriptor = next(field for field in after["textSemantics"]["fields"]
                                          if field["block"] == prose_id and field["path"] == "/text")
                        descriptor["sourceSha256"] = hashlib.sha256(
                            block["text"].encode("utf-8")
                        ).hexdigest()
                        self.assertTrue(all(block["text"][span["start"]:span["end"]]
                                            == span["text"] for span in descriptor["spans"]))
                    if revisions:
                        del meta["sourceFormatRevisions"]
                    del meta["editorial"][-len(appended):]
                    state.validate(after)
                    state.validate_transition(after, published)
                meta = after["stateLayout"]
                current = after["pages"][p]["blocks"][b]
                history = meta["orderRefinements"]
                self.assertEqual(1, len(history))
                self.assertEqual(identity, history[0]["id"])
                original = history[0]["block"]
                self.assertEqual(old_count, len(original["fragments"]))
                self.assertEqual(new_count, len(current["fragments"]))
                self.assertTrue(all(item.strip() and "\n" not in item and "\r" not in item
                                    for item in current["fragments"]))
                self.assertNotIn("fragmentos", current["question"])
                self.assertEqual([identity,identity],[e["id"] for e in meta["editorial"][-2:]])
                selectors = [f for f in after["textSemantics"]["fields"]
                             if f["block"]==identity and f["path"]=="/question"]
                self.assertEqual(1,len(selectors))
                current_field=selectors[0]
                self.assertEqual(hashlib.sha256(current["question"].encode("utf-8")).hexdigest(),
                                 current_field["sourceSha256"])
                # Rebuild the last authentic, published question and its semantic reading tags.
                old_question=original["question"]
                prior_question=deepcopy(after)
                prior_question["pages"][p]["blocks"][b]["question"]=old_question
                old_count_word="tres" if name=="lesson_declaraciones_y_firmas.json" else "cuatro"
                marker={"start":old_question.index(old_count_word),
                        "end":old_question.index(old_count_word)+len(old_count_word),
                        "text":old_count_word,"role":"data","reading":"natural"}
                old_spans=[marker]
                for span in current_field["spans"]:
                    index=old_question.index(span["text"])
                    old_spans.append(dict(span,start=index,end=index+len(span["text"])))
                old_spans.sort(key=lambda span:span["start"])
                restored=next(f for f in prior_question["textSemantics"]["fields"]
                              if f["block"]==identity and f["path"]=="/question")
                restored["sourceSha256"]=hashlib.sha256(old_question.encode("utf-8")).hexdigest()
                restored["spans"]=old_spans
                prior_question["stateLayout"]["editorial"].pop()
                state.validate(prior_question)
                state.validate_transition(prior_question, after)
                # Then reconstruct the original grouped cards. Both proofs remain append-only.
                prior_order=deepcopy(prior_question)
                prior_order["pages"][p]["blocks"][b]=deepcopy(original)
                prior_order["stateLayout"].pop("orderRefinements")
                prior_order["stateLayout"]["editorial"].pop()
                if name == "lesson_hacer_explicito_el_orden_de_las_operaciones.json":
                    self.assertEqual([{"activity": identity, "count": 3}],
                                     after["orderVisualSeparators"])
                    prior_order.pop("orderVisualSeparators")
                else:
                    self.assertEqual([{"activity": identity, "count": sep}],
                                     after["orderVisualSeparators"])
                state.validate(prior_order)
                state.validate_transition(prior_order,prior_question)
                self.assertEqual(state.validate(prior_order),state.validate(after))
                for field in ("origin", "family", "current", "additions"):
                    self.assertEqual(prior_order["stateLayout"][field],meta[field])
                forged=deepcopy(after)
                forged["stateLayout"]["orderRefinements"][0]["block"]["fragments"][0]+=" altered"
                with self.assertRaises(state.LayoutError):
                    state.validate(forged)

    def test_old_and_refined_programs_compile_and_run_identically(self):
        with tempfile.TemporaryDirectory(prefix="function-order-lines-") as folder:
            for i, (name, p, b, identity, old_count, new_count, stdin, expected, sep) in enumerate(CASES):
                after = json.loads((LESSONS / name).read_text(encoding="utf-8"))
                old = after["stateLayout"]["orderRefinements"][0]["block"]
                new = after["pages"][p]["blocks"][b]
                results = []
                for label, card in (("old", old), ("refined", new)):
                    path = Path(folder) / f"{i}-{label}.cpp"
                    binary = Path(folder) / f"{i}-{label}"
                    path.write_text(program(card), encoding="utf-8")
                    built = subprocess.run(["c++","-std=c++23","-O0","-o",str(binary),str(path)],
                                           capture_output=True,text=True,timeout=90)
                    self.assertEqual(0, built.returncode, identity + built.stderr)
                    run = subprocess.run([str(binary)],input=stdin,capture_output=True,
                                         text=True,timeout=15)
                    self.assertEqual(0, run.returncode, identity)
                    self.assertEqual("", run.stderr, identity)
                    results.append(run.stdout)
                self.assertEqual(results[0], results[1], identity)
                self.assertEqual(expected, results[1], identity)


if __name__ == "__main__":
    unittest.main()
