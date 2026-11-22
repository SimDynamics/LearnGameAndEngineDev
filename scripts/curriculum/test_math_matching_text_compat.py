"""Real U5L13 math matching text authoring, without writing curricular assets."""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import re
import unittest

import math_matching_text_compat as matching
import identifier_rename_archive_preview as rename_preview
import stable_state_layout as state
import text_semantics
import validar_json_programacion as contract


PATH = (Path(__file__).resolve().parents[2] /
        "app/src/main/assets/content/es/programming/cpp_fundamentals/"
        "colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json")
OWNER = "block_0007_0007"
OLD = "maxima"
NEW = "frecuencia_maxima_encontrada"
PROMPT = "/prompt"
FEEDBACK = "/feedback/correct"
CHANGED_SOURCES = (2, 3)


class RealFrequencyMatchingEditorial(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original_bytes = PATH.read_bytes()
        cls.lesson = json.loads(cls.original_bytes)
        state.validate(cls.lesson)
        cls.original = state._blocks(cls.lesson)[OWNER]
        assert cls.original["type"] == "math_puzzle"
        assert cls.original["responses"][0]["type"] == "math_matching_drag"
        cls.old_expected = deepcopy(
            cls.original["responses"][0]["validator"]["expected"])
        cls.old_sources = [
            item["id"] for item in cls.original["responses"][0]["sources"]]
        cls.old_targets = [
            item["id"] for item in cls.original["responses"][0]["targets"]]
        cls.original_marks = {
            item["path"]: item
            for item in cls.lesson["textSemantics"]["fields"]
            if item["block"] == OWNER
        }

    def revisions(self):
        before = self.original
        targets = {
            PROMPT: before["prompt"].replace(OLD, NEW),
            FEEDBACK: before["feedback"]["correct"].replace(OLD, NEW),
        }
        for index in CHANGED_SOURCES:
            for suffix in ("label", "content/0/text"):
                path = f"/responses/0/sources/{index}/{suffix}"
                targets[path] = matching._get(before, path).replace(OLD, NEW)

        desired = deepcopy(before)
        for path, value in targets.items():
            matching._set(desired, path, value)
        spans = {}
        for path in targets:
            if path not in text_semantics.public_text_fields(before):
                continue
            source = text_semantics.public_text_fields(desired)[path]
            previous = self.original_marks[path]["spans"]
            original_text = text_semantics.public_text_fields(before)[path]
            # Map every original UTF-16 marker by its *authored occurrence*.
            # A repeated technical word cannot silently acquire a different
            # selection just because a new identifier expanded nearby.
            selected = []
            for entry in previous:
                original_prefix = (
                    original_text.encode("utf-16-le")[:entry["start"] * 2]
                    .decode("utf-16-le"))
                occurrence = 1 + len(list(re.finditer(
                    re.escape(entry["text"]), original_prefix)))
                selected.append(text_semantics.select(
                    source, entry["text"].replace(OLD, NEW),
                    entry["role"], occurrence=occurrence,
                    reading=entry.get("reading", "auto")))
            spans[path] = selected
        return targets, spans

    def test_real_matching_review_preserves_card_and_grading_owners(self):
        targets, spans = self.revisions()
        candidate, proof = matching.preview_math_matching_text_review(
            self.lesson, OWNER, targets, spans)
        self.assertEqual(proof["paths"], sorted(targets))
        self.assertEqual(proof["beforeSha256"], state.fingerprint(self.original))
        revised = state._blocks(candidate)[OWNER]
        self.assertEqual(proof["afterSha256"], state.fingerprint(revised))
        self.assertTrue(matching.verify_matching_preview(self.lesson, candidate, proof))
        response = revised["responses"][0]
        self.assertEqual(response["validator"]["expected"], self.old_expected)
        self.assertEqual([item["id"] for item in response["sources"]], self.old_sources)
        self.assertEqual([item["id"] for item in response["targets"]], self.old_targets)
        for index in CHANGED_SOURCES:
            self.assertEqual(response["sources"][index]["label"],
                             response["sources"][index]["content"][0]["text"])
            self.assertIn(NEW,response["sources"][index]["label"])
        self.assertIn(NEW,revised["prompt"])
        self.assertIn(NEW,revised["feedback"]["correct"])
        self.assertEqual(
            state._blocks(candidate)["block_0007_0002"],
            state._blocks(self.lesson)["block_0007_0002"])
        self.assertEqual(candidate["stateLayout"]["origin"],
                         self.lesson["stateLayout"]["origin"])
        self.assertEqual(candidate["stateLayout"]["additions"],
                         self.lesson["stateLayout"]["additions"])
        self.assertEqual(len(candidate["stateLayout"]["editorial"]),
                         len(self.lesson["stateLayout"]["editorial"]) + 1)
        self.assertEqual(PATH.read_bytes(),self.original_bytes)

    def test_python_state_layout_authenticates_matching_text_and_restores_prior_reading(self):
        targets, spans = self.revisions()
        draft, proof = matching.preview_math_matching_text_review(
            self.lesson, OWNER, targets, spans)
        self.assertTrue(matching.verify_matching_preview(self.lesson,draft,proof))
        state.validate(draft)
        state.validate_transition(self.lesson, draft)
        aligned = state.align_for_previous_validation(self.lesson, draft)
        self.assertEqual(state._blocks(aligned), state._blocks(self.lesson))
        self.assertEqual(aligned.get("textSemantics"),
                         self.lesson.get("textSemantics"))
        self.assertEqual(state.original_projection(draft),
                         state.original_projection(self.lesson))
        # The direct curricular guard must share this authenticated contract
        # with P2 rather than relying on a course-specific exception.
        relative = PATH.relative_to(Path(__file__).resolve().parents[2]).as_posix()
        storage = state.validate(self.lesson)
        location = next(
            (p, b) for p, row in enumerate(self.lesson["stateLayout"]["current"])
            for b, identity in enumerate(row["blocks"]) if identity == OWNER)
        self.assertTrue(contract.preserves_previous_exercise(
            relative, *storage[location], self.original, state._blocks(draft)[OWNER],
            previous_document=self.lesson, current_document=draft,
            course_id="cpp_fundamentals"))
        no_proof = deepcopy(draft)
        del no_proof["stateLayout"]["mathMatchingTextRevisions"]
        with self.assertRaises(state.LayoutError):
            state.validate(no_proof)

    def test_python_loader_rejects_forged_math_matching_journal(self):
        targets, spans = self.revisions()
        draft, proof = matching.preview_math_matching_text_review(
            self.lesson, OWNER, targets, spans)
        state.validate(draft)
        for name, mutate in (
            ("wrong source",lambda d: d["pages"][7]["blocks"][7][
                "responses"][0]["sources"][2].update({"id":"forged"})),
            ("wrong expected",lambda d: d["pages"][7]["blocks"][7][
                "responses"][0]["validator"]["expected"].update({
                    "supera_maximo":"conservar_empate"})),
            ("forged prior hash",lambda d: d["stateLayout"][
                "mathMatchingTextRevisions"][0].update({"beforeSha256":"0"*64})),
            ("forged semantic",lambda d: d["stateLayout"][
                "mathMatchingTextRevisions"][0]["semanticBefore"].update({
                    PROMPT: None})),
            ("other owner",lambda d: d["stateLayout"][
                "mathMatchingTextRevisions"][0].update({"id":"block_0007_0004"})),
            ("missing editorial",lambda d: d["stateLayout"]["editorial"].pop()),
        ):
            with self.subTest(name=name):
                corrupt = deepcopy(draft)
                mutate(corrupt)
                with self.assertRaises(state.LayoutError):
                    state.validate(corrupt)
        # An unrelated yet well-formed marker cannot be disproved by the
        # current document alone. The *published predecessor* authenticates
        # it as part of the transition, not a guessed marker allowlist.
        other_marker=deepcopy(draft)
        marker=next(field for field in other_marker["textSemantics"]["fields"]
                    if field["block"] != OWNER and field["spans"])
        marker["spans"]=[]
        state.validate(other_marker)
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.lesson,other_marker)
        self.assertEqual(PATH.read_bytes(),self.original_bytes)

    def test_matching_source_label_must_match_its_text(self):
        targets, _ = self.revisions()
        after = deepcopy(self.original)
        for path, value in targets.items():
            matching._set(after,path,value)
        after["responses"][0]["sources"][2]["label"] = "Etiquetas de texto"
        with self.assertRaisesRegex(matching.MatchingTextError,
                                    "source text disagree"):
            matching.require_matching_text_only(
                self.original,after,sorted(targets))

    def test_undeclared_answer_and_order_changes_are_rejected(self):
        targets, _ = self.revisions()
        revised = deepcopy(self.original)
        for path,value in targets.items():
            matching._set(revised,path,value)
        for name, mutate in (
            ("mapping",lambda d: d["responses"][0]["validator"]["expected"].update(
                {"supera_maximo":"conservar_empate"})),
            ("source ID",lambda d: d["responses"][0]["sources"][2].update(
                {"id":"invented_owner"})),
            ("target label",lambda d: d["responses"][0]["targets"][0].update(
                {"label":"changed outside the declared scope"})),
            ("source order",lambda d: d["responses"][0]["sources"].reverse()),
            ("extra response",lambda d: d["responses"].append(
                deepcopy(d["responses"][0]))),
        ):
            with self.subTest(name=name):
                forged=deepcopy(revised)
                mutate(forged)
                with self.assertRaises(matching.MatchingTextError):
                    matching.require_matching_text_only(
                        self.original,forged,sorted(targets))

    def test_forged_or_missing_semantics_are_rejected(self):
        targets, spans = self.revisions()
        self.assertTrue(spans[PROMPT])
        for path in (PROMPT, FEEDBACK,
                     "/responses/0/sources/2/content/0/text"):
            bad=deepcopy(spans)
            bad.pop(path)
            with self.subTest(path=path):
                with self.assertRaises(matching.MatchingTextError):
                    matching.preview_math_matching_text_review(
                        self.lesson,OWNER,targets,bad)
        before, proof = matching.preview_math_matching_text_review(
            self.lesson,OWNER,targets,spans)
        for field,value in (
            ("beforeSha256","0"*64),
            ("afterSha256","0"*64),
            ("historySha256","0"*64),
            ("editorialIndex",999),
            ("paths",["/prompt"]),
            ("rawJson",'{"type":"math_puzzle","prompt":"forged"}'),
        ):
            altered=deepcopy(proof);altered[field]=value
            with self.subTest(field=field):
                with self.assertRaises(matching.MatchingTextError):
                    matching.verify_matching_preview(self.lesson,before,altered)
        replaced=deepcopy(before)
        for field in replaced["textSemantics"]["fields"]:
            if field["block"]==OWNER and field["path"]==PROMPT:
                field["sourceSha256"]="0"*64
        with self.assertRaises(matching.MatchingTextError):
            matching.verify_matching_preview(self.lesson,replaced,proof)

    def test_six_compiler_names_and_matching_text_compose_in_either_order(self):
        """Both independent authoring orders must keep original code and card credit."""
        plans = {
            "block_0006_0002": {"code": {
                "maxima": "frecuencia_maxima_encontrada",
                "mayores": "etiquetas_mas_frecuentes"}},
            "recuperacion_entrada_editor": {"initialCode": {
                "maxima": "frecuencia_maxima_encontrada",
                "mayores": "etiquetas_mas_frecuentes"}},
            "block_0006_0009": {"initialCode": {
                "maxima": "frecuencia_maxima_encontrada",
                "mayores": "etiquetas_mas_frecuentes"}},
            "block_0007_0002": {"code": {
                "maxima": "frecuencia_maxima_encontrada",
                "mayores": "etiquetas_mas_frecuentes"}},
            "block_0007_0004": {"code": {
                "maxima": "frecuencia_maxima_encontrada",
                "mayores": "etiquetas_mas_frecuentes"}},
            "frecuencias_numericas_referencia_programa": {"code": {
                "maxima": "frecuencia_maxima_encontrada",
                "mayores": "valores_mas_frecuentes"}},
        }
        changes, spans = self.revisions()
        source_first = rename_preview.prepare_declared_identifier_rename_draft(
            self.lesson, plans)
        names_then_text, last_text_proof = matching.preview_math_matching_text_review(
            source_first, OWNER, changes, spans)
        text_first, early_text_proof = matching.preview_math_matching_text_review(
            self.lesson, OWNER, changes, spans)
        text_then_names = rename_preview.prepare_declared_identifier_rename_draft(
            text_first, plans)

        for parent, child in (
                (self.lesson, source_first),
                (source_first, names_then_text),
                (self.lesson, text_first),
                (text_first, text_then_names),
                (self.lesson, names_then_text),
                (self.lesson, text_then_names)):
            with self.subTest(edge=len(parent["stateLayout"]["editorial"])):
                state.validate(parent)
                state.validate(child)
                state.validate_transition(parent, child)
        self.assertEqual(names_then_text["pages"], text_then_names["pages"])
        self.assertEqual(names_then_text["textSemantics"],
                         text_then_names["textSemantics"])
        self.assertEqual(state.original_projection(names_then_text),
                         state.original_projection(self.lesson))
        self.assertEqual(state.original_projection(text_then_names),
                         state.original_projection(self.lesson))
        self.assertEqual(
            state._blocks(state.align_for_previous_validation(
                self.lesson, names_then_text)),
            state._blocks(self.lesson))
        self.assertEqual(
            state._blocks(state.align_for_previous_validation(
                self.lesson, text_then_names)),
            state._blocks(self.lesson))
        self.assertEqual(
            last_text_proof["beforeSha256"], early_text_proof["beforeSha256"])
        self.assertEqual(
            last_text_proof["afterSha256"], early_text_proof["afterSha256"])
        for result in (names_then_text, text_then_names):
            metadata = result["stateLayout"]
            self.assertEqual(
                {entry["id"] for entry in metadata["identifierRenameRevisions"]},
                set(plans))
            self.assertEqual(
                [entry["id"] for entry in metadata["mathMatchingTextRevisions"]],
                [OWNER])
            self.assertEqual(
                self.original["responses"][0]["validator"]["expected"],
                state._blocks(result)[OWNER]["responses"][0]["validator"]["expected"])
            self.assertEqual(
                [card["id"] for card in self.original["responses"][0]["sources"]],
                [card["id"] for card in
                 state._blocks(result)[OWNER]["responses"][0]["sources"]])
        self.assertEqual(PATH.read_bytes(), self.original_bytes)

    def test_combined_rename_and_matching_rejects_cross_owner_forgery(self):
        plans = {"block_0007_0002": {"code": {
            "maxima": "frecuencia_maxima_encontrada",
            "mayores": "etiquetas_mas_frecuentes"}}}
        targets, spans = self.revisions()
        renamed = rename_preview.prepare_declared_identifier_rename_draft(
            self.lesson, plans)
        combined, _ = matching.preview_math_matching_text_review(
            renamed, OWNER, targets, spans)
        state.validate_transition(self.lesson, combined)
        for label, mutation in (
            ("missing matching history", lambda d: d["stateLayout"][
                "mathMatchingTextRevisions"].pop()),
            ("foreign rename proof", lambda d: d["stateLayout"][
                "identifierRenameRevisions"][0].update({
                    "beforeSha256": "0" * 64})),
            ("foreign card identity", lambda d: state._blocks(d)[OWNER][
                "responses"][0]["sources"][2].update({"id": "invented"})),
            ("unreviewed text mark", lambda d: next(
                f for f in d["textSemantics"]["fields"]
                if f["block"] == OWNER and f["path"] == PROMPT
            ).update({"sourceSha256": "0" * 64})),
        ):
            with self.subTest(forgery=label):
                forged = deepcopy(combined)
                mutation(forged)
                with self.assertRaises(state.LayoutError):
                    state.validate(forged)
        self.assertEqual(PATH.read_bytes(), self.original_bytes)

    def test_only_supported_text_and_matching_type_can_be_reviewed(self):
        targets, spans = self.revisions()
        for invalid in (
            {"/responses/0/validator/expected/supera_maximo":"bad"},
            {"/responses/0/sources/2/id":"other"},
            {"/responses/0/sources/2/label":""},
            {"/responses/0/sources/2/content/0/text":"value without card label"},
        ):
            with self.subTest(invalid=invalid):
                with self.assertRaises(matching.MatchingTextError):
                    matching.preview_math_matching_text_review(
                        self.lesson,OWNER,invalid,{})
        renamed=deepcopy(self.lesson)
        for page,record in zip(
                renamed["pages"],renamed["stateLayout"]["current"]):
            for identity,block in zip(record["blocks"],page["blocks"]):
                if identity==OWNER:
                    block["responses"][0]["type"]="math_numeric_input"
        with self.assertRaises((matching.MatchingTextError,state.LayoutError)):
            matching.preview_math_matching_text_review(renamed,OWNER,targets,spans)
        self.assertEqual(PATH.read_bytes(),self.original_bytes)



class RealAssetCheckLineEndingPrompt(unittest.TestCase):
    """The same matching-text authoring must also work in Programación 2."""

    def test_published_assetcheck_prompt_retains_exact_mapping_and_history(self):
        asset = (Path(__file__).resolve().parents[2] /
            "app/src/main/assets/content/es/programming/programming_practice_tools/"
            "proyecto_assetcheck/lesson_leer_manifiesto_y_publicar_informe.json")
        initial_bytes = asset.read_bytes()
        previous = json.loads(initial_bytes)
        state.validate(previous)
        identity = "block_0002_0005"
        old = state._blocks(previous)[identity]
        self.assertEqual(old["id"], "assetcheck_archivo_marcas_de_linea")
        self.assertEqual(old["responses"][0]["type"], "math_matching_drag")
        old_phrase = ("Las barras inclinadas representan caracteres de escape, "
                      "no texto literal del manifiesto.")
        new_phrase = ("En las etiquetas, las secuencias con barra inversa "
                      "representan LF o CR; no son una barra y una letra "
                      "escritas en el manifiesto.")
        if old["prompt"].count(new_phrase) == 1:
            # A published authorial correction is the goal of this test.
            # Once Work delivers it, validate its immutable answer history
            # instead of trying to rewrite the already-reviewed text again.
            journals = [
                item for item in previous["stateLayout"].get(
                    "mathMatchingTextRevisions", [])
                if item.get("id") == identity]
            self.assertEqual(len(journals), 1)
            archived = state.parse_json(journals[0]["rawJson"])
            self.assertEqual(archived["prompt"].count(old_phrase), 1)
            self.assertEqual(old["responses"], archived["responses"])
            self.assertEqual(journals[0]["paths"], ["/prompt"])
            positions = state.validate(previous)
            location = next(
                (p, b) for p, page in enumerate(
                    previous["stateLayout"]["current"])
                for b, owner in enumerate(page["blocks"])
                if owner == identity)
            origin = positions[location]
            historical = state.original_projection(previous)[
                "pages"][origin[0]]["blocks"][origin[1]]
            self.assertEqual(historical["prompt"], archived["prompt"])
            marker = next(
                field for field in previous["textSemantics"]["fields"]
                if field["block"] == identity and field["path"] == "/prompt")
            # A corrected prompt may have new authored technical spans.
            # Its UTF-16 ranges are validated by state.validate(previous);
            # require the exact text owner rather than forcing blank marks.
            self.assertIsInstance(marker["spans"], list)
            self.assertEqual(marker["sourceSha256"],
                             state.hashlib.sha256(
                                 old["prompt"].encode("utf-8")).hexdigest())
            for name, tamper in (
                ("answer mapping", lambda d: state._blocks(d)[identity][
                    "responses"][0]["validator"]["expected"].update(
                    {"a": "dos"})),
                ("forged history", lambda d: d["stateLayout"][
                    "mathMatchingTextRevisions"][0].update({
                        "afterSha256": "0" * 64})),
                ("forged text marker", lambda d: next(
                    field for field in d["textSemantics"]["fields"]
                    if field["block"] == identity
                    and field["path"] == "/prompt"
                ).update({"sourceSha256": "0" * 64})),
            ):
                forged = deepcopy(previous)
                tamper(forged)
                with self.subTest(tamper=name), self.assertRaises(
                        state.LayoutError):
                    state.validate(forged)
            self.assertEqual(asset.read_bytes(), initial_bytes)
            return
        self.assertEqual(old["prompt"].count(old_phrase), 1)
        revised_prompt = old["prompt"].replace(old_phrase, new_phrase)
        updated, proof = matching.preview_math_matching_text_review(
            previous, identity, {"/prompt": revised_prompt}, {"/prompt": []})
        self.assertTrue(matching.verify_matching_preview(previous, updated, proof))
        state.validate(updated)
        state.validate_transition(previous, updated)
        revised = state._blocks(updated)[identity]
        self.assertEqual(revised["prompt"], revised_prompt)
        self.assertEqual(revised["responses"], old["responses"])
        self.assertEqual(revised["id"], old["id"])
        self.assertEqual(updated["stateLayout"]["origin"],
                         previous["stateLayout"]["origin"])
        self.assertEqual(updated["stateLayout"]["current"],
                         previous["stateLayout"]["current"])
        self.assertEqual(state._blocks(state.align_for_previous_validation(
            previous, updated)), state._blocks(previous))
        self.assertEqual(state.original_projection(updated),
                         state.original_projection(previous))
        relative = asset.relative_to(Path(__file__).resolve().parents[2]).as_posix()
        original_positions = state.validate(previous)
        location = next((p, b)
            for p, page in enumerate(previous["stateLayout"]["current"])
            for b, owner in enumerate(page["blocks"]) if owner == identity)
        storage = original_positions[location]
        self.assertTrue(contract.preserves_previous_exercise(
            relative, *storage, old, revised,
            previous_document=previous, current_document=updated,
            course_id="programming_practice_tools"))
        # This is not a legacy exemption for another course, unknown lesson or
        # shifted owner; even an authentic journal must match its origin.
        for invalid in (
            {"relative": relative.replace("programming_practice_tools",
                                          "cpp_fundamentals")},
            {"course_id": "cpp_fundamentals"},
            {"pn": storage[0] + 1},
            {"bn": storage[1] + 1},
        ):
            with self.subTest(wrong_binding=invalid):
                arguments = {"relative": relative, "pn": storage[0],
                             "bn": storage[1],
                             "course_id": "programming_practice_tools"}
                arguments.update(invalid)
                self.assertFalse(contract.preserves_previous_exercise(
                    arguments["relative"], arguments["pn"], arguments["bn"],
                    old, revised, previous_document=previous,
                    current_document=updated, course_id=arguments["course_id"]))
        self.assertFalse(contract.preserves_previous_exercise(
            relative, *storage, old, revised, course_id="programming_practice_tools"))
        self.assertEqual(asset.read_bytes(), initial_bytes)

        # A prose edit never authorizes changing source/target IDs, the
        # evaluated matching pairs or the original reading provenance.
        for name, tamper in (
            ("answer mapping", lambda d: state._blocks(d)[identity][
                "responses"][0]["validator"]["expected"].update({"a": "dos"})),
            ("new source ID", lambda d: state._blocks(d)[identity][
                "responses"][0]["sources"][0].update({"id": "different"})),
            ("forged history", lambda d: d["stateLayout"][
                "mathMatchingTextRevisions"][0].update({"afterSha256": "0" * 64})),
            ("missing journal", lambda d: d["stateLayout"].pop(
                "mathMatchingTextRevisions")),
            ("forged text marker", lambda d: next(
                field for field in d["textSemantics"]["fields"]
                if field["block"] == identity and field["path"] == "/prompt"
            ).update({"sourceSha256": "0" * 64})),
        ):
            forged = deepcopy(updated)
            tamper(forged)
            with self.subTest(tamper=name):
                with self.assertRaises(state.LayoutError):
                    state.validate(forged)
                self.assertFalse(contract.preserves_previous_exercise(
                    relative, *storage, old, state._blocks(forged)[identity],
                    previous_document=previous, current_document=forged,
                    course_id="programming_practice_tools"))
        self.assertEqual(asset.read_bytes(), initial_bytes)



if __name__=="__main__":
    unittest.main()
