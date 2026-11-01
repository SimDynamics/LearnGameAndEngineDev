"""Adversarial segment-construction contracts through the real Windows worker.

Fixtures describe technical geometry only. They do not add activities, answers or
progress to the curriculum, and intentionally run without installed voice models.
"""
from __future__ import annotations

import copy
import json
import math
import re
import unittest

from test_worker import Worker


def point(x, y, label=None):
    value = {"x": x, "y": y}
    if label is not None:
        value["label"] = label
    return value


def graph(points, draft=None):
    value = {"type": "graph", "points": [point(x, y) for x, y in points]}
    if draft is not None:
        value["interactionDraft"] = copy.deepcopy(draft)
    return value


def chain(lengths=(3.0, 5.0), origin=(0.0, 0.0), angle=0.0):
    # Source placement/orientation is deliberately unrelated to the target ray.
    sources = []
    for index, length in enumerate(lengths):
        direction = .63 + index * .91
        start = (-7.0 + index * 8.0, 8.0 + index * 2.0)
        sources.append({"id": f"source_{index}",
                        "from": point(*start, f"A{index}"),
                        "to": point(start[0] + length * math.cos(direction),
                                    start[1] + length * math.sin(direction), f"B{index}")})
    return {"type": "segment_transport", "mode": "chain", "sources": sources,
            "target": {"origin": point(*origin, "M"),
                       "direction": point(math.cos(angle), math.sin(angle))},
            "steps": [source["id"] for source in sources],
            "pointLabels": [f"N{index}" for index in range(len(lengths))],
            "positionTolerance": .04, "lengthTolerance": .04}


def partition(count=3, length=6.0, origin=(0.0, 0.0), angle=0.0):
    return {"type": "segment_transport", "mode": "partition", "partitionCount": count,
            "target": {"origin": point(*origin, "M"),
                       "end": point(origin[0] + length * math.cos(angle),
                                    origin[1] + length * math.sin(angle), "P")},
            "pointLabels": [f"N{index}" for index in range(count - 1)],
            "positionTolerance": .04, "lengthTolerance": .04}


def positions(spec, distances):
    origin = spec["target"]["origin"]
    direction = spec["target"].get("direction")
    if direction is None:
        end = spec["target"]["end"]
        direction = point(end["x"] - origin["x"], end["y"] - origin["y"])
    magnitude = math.hypot(direction["x"], direction["y"])
    return [(origin["x"] + distance * direction["x"] / magnitude,
             origin["y"] + distance * direction["y"] / magnitude) for distance in distances]


def block(spec):
    return {"type": "math_puzzle", "id": "technical_segment_transport_fixture",
            "prompt": "Construye las longitudes conservando sus extremos y su sentido.",
            "feedback": {"correct": "Comprobación técnica completada."},
            "responses": [{"id": "construction", "type": "math_segment_transport",
                           "validator": copy.deepcopy(spec)}]}


class WindowsSegmentTransportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.worker = Worker(missing_resources=True)

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    def evaluate(self, spec, answer, language="es"):
        return self.worker.ask("validate_math", block=block(spec),
                               responses={"construction": answer}, language=language)

    def draw(self, spec, state=None, gesture=None, **fields):
        request = {"action": "segment_layout", "config": spec,
                   "width": 850, "height": 600, "unit": 1, "textSize": 15, **fields}
        if state is not None:
            request["state"] = state
        if gesture is not None:
            request["gesture"] = gesture
        return self.worker.ask("math_editor", **request)

    def fatal(self, command, **fields):
        identity = self.worker.send(command, **fields)
        reply = self.worker.receive()
        self.assertEqual(identity, reply.get("id"), reply)
        self.assertEqual(command, reply.get("command"), reply)
        self.assertIn("error", reply, "Invalid technical configuration became a learner verdict")
        self.assertNotEqual("incorrect", reply.get("feedbackKind"), reply)
        return reply

    def assert_correct(self, spec, points):
        result = self.evaluate(spec, graph(points))
        self.assertTrue(result["evaluable"], result)
        self.assertTrue(result["correct"], result)
        self.assertEqual("correct", result["feedbackKind"])

    def assert_wrong(self, spec, points):
        result = self.evaluate(spec, graph(points))
        self.assertTrue(result["evaluable"], result)
        self.assertFalse(result["correct"], result)
        self.assertEqual("incorrect", result["feedbackKind"])
        return result

    def test_01_chain_is_invariant_under_target_and_source_orientation(self):
        for origin in ((0, 0), (13, -9), (-21, 17)):
            for angle in (0, math.pi / 2, -1.27, 2.34):
                with self.subTest(origin=origin, angle=angle):
                    spec = chain(origin=origin, angle=angle)
                    self.assert_correct(spec, positions(spec, [3, 8]))
                    # Source array order is editorial, while steps retain identity.
                    spec["sources"].reverse()
                    self.assert_correct(spec, positions(spec, [3, 8]))

    def test_02_total_does_not_replace_individual_lengths_or_directed_order(self):
        spec = chain()
        for distances in ([4, 8], [5, 8], [8, 3], [3, 3], [-3, 2], [0, 8]):
            with self.subTest(distances=distances):
                self.assert_wrong(spec, positions(spec, distances))
        # Correct distances from M alone do not establish ray membership.
        self.assert_wrong(spec, [(0, 3), (8, 0)])
        self.assert_wrong(spec, [(3, .2), (8, 0)])

    def test_03_repeated_copies_check_every_part_and_accumulated_total(self):
        spec = chain((3,))
        spec["steps"] *= 8
        spec["pointLabels"] = [f"N{index}" for index in range(8)]
        self.assert_correct(spec, positions(spec, [3 * index for index in range(1, 9)]))
        # Every local error is below tolerance, but the total drifts beyond it.
        self.assert_wrong(spec, positions(spec, [3.025 * index for index in range(1, 9)]))
        for distances in ([3, 6, 10, 12, 15, 18, 21, 24],
                          [3, 6, 9, 9, 15, 18, 21, 24],
                          [3, 6, 9, 15, 12, 18, 21, 24]):
            with self.subTest(distances=distances):
                self.assert_wrong(spec, positions(spec, distances))

    def test_04_partition_requires_equal_parts_covering_the_given_total(self):
        for count, length, angle in ((2, 6, 0), (3, 9, .71), (5, 10, -1.82)):
            with self.subTest(count=count, angle=angle):
                spec = partition(count, length, origin=(11, -4), angle=angle)
                self.assert_correct(spec, positions(spec, [length * i / count for i in range(1, count)]))
        spec = partition()
        for distances in ([1.5, 3], [2.9, 4.9], [4, 2], [2, 2], [-2, 4], [2, 8]):
            with self.subTest(distances=distances):
                self.assert_wrong(spec, positions(spec, distances))
        self.assert_wrong(partition(2), [(3, .2)])

    def test_05_partial_and_cross_type_answers_never_become_an_accreditation(self):
        spec = chain()
        for answer in (graph([]), graph([(3, 0)]),
                       {"type": "text", "value": "3, 8"},
                       {"type": "rigid_angle", "x": 3, "y": 0, "rotation": 0}):
            with self.subTest(answer=answer):
                result = self.evaluate(spec, answer)
                self.assertFalse(result["correct"], result)
                self.assertFalse(result["evaluable"], result)
        self.assertFalse(self.evaluate(spec, graph([(3, 0), (8, 0), (11, 0)]))["correct"])

    def test_06_describe_and_error_feedback_never_return_constructed_solutions(self):
        spec = chain((2.731, 4.619))
        described = self.worker.ask("describe_math", block=block(spec), language="es")
        self.assertEqual("math_segment_transport", described["responses"][0]["type"])
        forbidden = {"validator", "expected", "solution", "correctFeedback", "incorrectFeedback",
                     "reviewFeedback", "correctPoints", "answer", "answers"}

        def inspect(value):
            if isinstance(value, dict):
                self.assertFalse(forbidden.intersection(value), value)
                for child in value.values():
                    inspect(child)
            elif isinstance(value, list):
                for child in value:
                    inspect(child)

        inspect(described)
        for language in ("es", "fr", "en-US"):
            result = self.evaluate(spec, graph([(1, 1), (2, 2)]), language)
            self.assertFalse(result["correct"])
            feedback = result["feedback"]
            for hidden in ("2.731", "4.619", "7.35", "Comprobación técnica completada."):
                self.assertNotIn(hidden, feedback)
            self.assertTrue(feedback.strip())

    def test_07_invalid_geometry_is_a_configuration_error_before_evaluation(self):
        candidates = []

        def changed(modify):
            candidate = chain()
            modify(candidate)
            candidates.append(candidate)

        changed(lambda spec: spec["sources"][0].update(to=copy.deepcopy(spec["sources"][0]["from"])))
        changed(lambda spec: spec["target"].update(direction=point(0, 0)))
        changed(lambda spec: spec.update(positionTolerance=0))
        changed(lambda spec: spec.update(positionTolerance="NaN"))
        changed(lambda spec: spec.update(lengthTolerance=-1))
        changed(lambda spec: spec.update(lengthTolerance=1))
        changed(lambda spec: spec.update(steps=["absent"]))
        changed(lambda spec: spec.update(steps=[]))
        changed(lambda spec: spec["target"]["origin"].update(x="Infinity"))
        changed(lambda spec: spec["sources"][1].update(id=spec["sources"][0]["id"]))
        changed(lambda spec: spec.update(pointLabels=["N"]))
        changed(lambda spec: spec.update(initial={"points": [point(1, 0), point(2, 0), point(3, 0)]}))
        invalid_partition = partition()
        invalid_partition["target"]["end"] = copy.deepcopy(invalid_partition["target"]["origin"])
        candidates.append(invalid_partition)
        for count in (0, 1, 2.5):
            candidate = partition()
            candidate["partitionCount"] = count
            candidates.append(candidate)
        tiny_parts = partition(32, 1e-9)
        tiny_parts.update(positionTolerance=1e-13, lengthTolerance=1e-13)
        candidates.append(tiny_parts)
        for index, spec in enumerate(candidates):
            with self.subTest(configuration=index):
                original = copy.deepcopy(spec)
                self.fatal("describe_math", block=block(spec), language="es")
                self.fatal("validate_math", block=block(spec), responses={"construction": graph([(3, 0), (8, 0)])}, language="es")
                self.assertEqual(original, spec)
        self.assert_correct(chain(), [(3, 0), (8, 0)])

    def test_08_geometric_tolerance_is_bounded_and_independent_of_screen_size(self):
        spec = chain((3,))
        inside = graph([(3.01, .01)])
        outside = graph([(3.1, .1)])
        for width, height, unit in ((400, 280, .75), (850, 600, 1), (1700, 1200, 2)):
            with self.subTest(width=width, unit=unit):
                small = self.draw(spec, inside, width=width, height=height, unit=unit)
                large = self.draw(spec, outside, width=width, height=height, unit=unit)
                self.assertEqual(inside["points"], small["state"]["points"])
                self.assertEqual(outside["points"], large["state"]["points"])
                self.assertTrue(self.evaluate(spec, small["state"])["correct"])
                self.assertFalse(self.evaluate(spec, large["state"])["correct"])

    def test_09_camera_and_tool_drafts_do_not_change_geometric_verdicts(self):
        spec = chain()
        draft = {"mode": "view", "activePoint": 1,
                 "camera": {"x": 300, "y": -200, "zoom": 2},
                 "tool": {"x": 20, "y": -10, "rotation": 1.27, "length": 100}}
        for points, correct in (([(3, 0), (8, 0)], True), ([(4, 0), (8, 0)], False)):
            with self.subTest(correct=correct):
                answer = graph(points, draft)
                result = self.evaluate(spec, answer)
                self.assertEqual(correct, result["correct"], result)
                restored = self.draw(spec, json.loads(json.dumps(answer)))
                self.assertEqual(answer["points"], restored["state"]["points"])
                self.assertEqual(draft, restored["state"]["interactionDraft"])
                self.assertEqual(correct, self.evaluate(spec, restored["state"])["correct"])

    def test_10_placing_is_free_and_never_snaps_to_the_solution(self):
        spec = chain((3,))
        initial = self.draw(spec)
        self.assertEqual([], initial["state"]["points"])
        placed = self.draw(spec, initial["state"], {"type": "place", "index": 0,
                                                   "current": point(311.37, 219.41)})
        moved = self.draw(spec, placed["state"], {"type": "move_point", "index": 0,
                                                 "current": point(312.37, 220.41)})
        self.assertEqual(1, len(placed["state"]["points"]))
        self.assertNotEqual(placed["state"]["points"], moved["state"]["points"])
        # This arbitrary off-ray attempt is retained verbatim on re-layout.
        result = self.evaluate(spec, placed["state"])
        self.assertFalse(result["correct"], result)
        self.assertEqual(placed["state"]["points"], self.draw(spec, placed["state"])["state"]["points"])

    def test_11_measuring_captures_a_length_until_explicit_recapture(self):
        spec = chain()
        measured = self.draw(spec, graph([(3, 0), (8, 0)]), {"type": "measure", "id": "part:0"})
        self.assertAlmostEqual(3, measured["state"]["interactionDraft"]["tool"]["length"])
        changed = copy.deepcopy(measured["state"])
        changed["points"][0] = point(4.5, 0)
        retained = self.draw(spec, changed)
        self.assertEqual(measured["state"]["interactionDraft"]["tool"], retained["state"]["interactionDraft"]["tool"])
        recaptured = self.draw(spec, retained["state"], {"type": "measure", "id": "part:0"})
        self.assertAlmostEqual(4.5, recaptured["state"]["interactionDraft"]["tool"]["length"])
        source = self.draw(spec, changed, {"type": "measure", "id": "source:source_1"})
        self.assertAlmostEqual(5, source["state"]["interactionDraft"]["tool"]["length"])
        total = self.draw(partition(), graph([(2, 0), (4, 0)]), {"type": "measure", "id": "target"})
        self.assertAlmostEqual(6, total["state"]["interactionDraft"]["tool"]["length"])

    def test_12_moving_and_rotating_the_tool_preserve_its_opening_and_answers(self):
        spec = chain()
        drawing = self.draw(spec, graph([(3, 0), (8, 0)]), {"type": "measure", "id": "source:source_0"})
        before = drawing["state"]
        translated = self.draw(spec, before, {"type": "move_tool", "previous": point(300, 300),
                                             "current": point(323, 317)})
        old_tool = before["interactionDraft"]["tool"]
        new_tool = translated["state"]["interactionDraft"]["tool"]
        self.assertEqual(old_tool["length"], new_tool["length"])
        self.assertEqual(old_tool["rotation"], new_tool["rotation"])
        self.assertNotEqual((old_tool["x"], old_tool["y"]), (new_tool["x"], new_tool["y"]))
        center = translated["tool"]["center"]
        rotated = self.draw(spec, translated["state"], {"type": "rotate_tool",
                            "previous": point(center["x"] + 80, center["y"]),
                            "current": point(center["x"], center["y"] + 80)})
        rotated_tool = rotated["state"]["interactionDraft"]["tool"]
        self.assertEqual(new_tool["length"], rotated_tool["length"])
        self.assertEqual((new_tool["x"], new_tool["y"]), (rotated_tool["x"], rotated_tool["y"]))
        self.assertNotEqual(new_tool["rotation"], rotated_tool["rotation"])
        for snapshot in (translated, rotated):
            self.assertEqual(before["points"], snapshot["state"]["points"])
            self.assertTrue(self.evaluate(spec, snapshot["state"])["correct"])

    def test_13_camera_navigation_preserves_out_of_frame_attempts_and_solution(self):
        spec = chain((30,))
        # Correct end lies well beyond the initial short portion of the ray.
        original = graph([(30, 0)])
        drawing = self.draw(spec, original)
        moved = self.draw(spec, drawing["state"], {"type": "view", "previous": point(300, 300),
                                                  "current": point(-700, 800)})
        zoomed = self.draw(spec, moved["state"], {"type": "zoom", "current": point(400, 300), "delta": 2})
        for snapshot in (drawing, moved, zoomed):
            self.assertEqual(original["points"], snapshot["state"]["points"])
            self.assertTrue(self.evaluate(spec, snapshot["state"])["correct"])
        self.assertNotEqual(drawing["state"]["interactionDraft"]["camera"], moved["state"]["interactionDraft"]["camera"])
        reset = self.draw(spec, zoomed["state"], {"type": "reset_view"})
        self.assertEqual(original["points"], reset["state"]["points"])
        extreme = graph([(10000.25, -20000.5)])
        self.assertEqual(extreme["points"], self.draw(spec, extreme)["state"]["points"])

    def test_14_layout_has_no_axes_numeric_scale_or_unbuilt_answer_markers(self):
        spec = chain()
        initial = self.draw(spec)
        self.assertEqual([], initial["constructedPoints"])
        self.assertEqual([], initial["state"]["points"])
        self.assertFalse(initial.get("axes"))
        self.assertFalse(initial.get("grid"))
        self.assertFalse(initial.get("ticks"))
        labels = [label["text"] for label in initial.get("labels", [])]
        self.assertFalse(set(spec["pointLabels"]).intersection(labels), labels)
        self.assertFalse(any(re.fullmatch(r"[-+]?\d+(?:[.,]\d+)?", label.strip()) for label in labels), labels)
        self.assertFalse(any(re.search(r"\(\s*[-+]?\d+(?:\.\d+)?\s*,\s*[-+]?\d+", label) for label in labels), labels)
        self.assertFalse(any(item.get("id", "").startswith("point:") for item in initial.get("points", [])))
        self.assertEqual({"source:source_0", "source:source_1"}, {item["id"] for item in initial["measurements"]})
        saved = copy.deepcopy(spec)
        self.draw(spec, graph([(1, 2)]), {"type": "measure", "id": "source:source_0"})
        self.assertEqual(saved, spec)

    def test_15_homogeneous_micro_geometry_keeps_one_reachable_pixel_scale(self):
        # Equivalent internal units must not turn a visible, usable segment
        # into a subpixel mark. Check actual paths against their endpoint dots,
        # not merely that the mathematical answer survives serialization.
        def fixture(scale):
            return {"type": "segment_transport", "mode": "chain",
                    "sources": [{"id": "ab", "from": point(0, scale, "A"),
                                 "to": point(3 * scale, scale, "B")}],
                    "target": {"origin": point(0, 0, "M"), "direction": point(scale, 0)},
                    "steps": ["ab"], "pointLabels": ["N"],
                    "positionTolerance": .01 * scale, "lengthTolerance": .01 * scale}

        reference = None
        for scale in (1.0, 1e-6):
            with self.subTest(scale=scale):
                spec = fixture(scale)
                drawing = self.draw(spec, graph([(3 * scale, 0)]))
                dots = {item["id"]: item["center"] for item in drawing["points"]}
                paths = {item["id"]: item for item in drawing["paths"]}
                for path_id, first_id, last_id in (
                    ("source:ab", "source:ab:from", "source:ab:to"),
                    ("part:0", "target:origin", "point:0"),
                    ("tool:segment", "tool:start", "tool:end"),
                ):
                    self.assertIn(path_id, paths, "A nondegenerate visible segment disappeared")
                    for end, identity in (("start", first_id), ("end", last_id)):
                        self.assertIn(identity, dots)
                        self.assertAlmostEqual(dots[identity]["x"], paths[path_id][end]["x"], delta=.05)
                        self.assertAlmostEqual(dots[identity]["y"], paths[path_id][end]["y"], delta=.05)
                source_start, source_end = dots["source:ab:from"], dots["source:ab:to"]
                length_pixels = math.hypot(source_end["x"] - source_start["x"],
                                           source_end["y"] - source_start["y"])
                self.assertGreater(length_pixels, 100, "Unit choice made the construction unreachable by pointer")
                # Transport the displayed source span from the given origin,
                # using a pointer gesture into an initially empty construction.
                origin = dots["target:origin"]
                at = point(origin["x"] + source_end["x"] - source_start["x"],
                           origin["y"] + source_end["y"] - source_start["y"])
                placed = self.draw(spec, gesture={"type": "place", "index": 0, "current": at})
                self.assertTrue(self.evaluate(spec, placed["state"])["correct"])
                if reference is None:
                    reference = dots
                else:
                    self.assertEqual(set(reference), set(dots))
                    for identity in reference:
                        for axis in ("x", "y"):
                            self.assertAlmostEqual(reference[identity][axis], dots[identity][axis], delta=.05,
                                                   msg=f"Homogeneous units changed the pixel placement of {identity}")

    def test_16_extreme_saved_geometry_is_render_safe_and_recovers_by_panning(self):
        spec = chain((3,))
        far_position = (1e9 - 10, -1e9 + 10)
        extreme = graph([far_position], {"mode": "view", "activePoint": 0,
                                        "camera": {"x": 0, "y": 0, "zoom": 64}})

        def assert_safe(drawing):
            # State remains exact world geometry. Only presentation primitives
            # are constrained to numbers Win32 LONG can represent safely.
            self.assertEqual(extreme["points"], drawing["state"]["points"])

            def numbers(value):
                if isinstance(value, dict):
                    for child in value.values():
                        yield from numbers(child)
                elif isinstance(value, list):
                    for child in value:
                        yield from numbers(child)
                elif isinstance(value, (int, float)) and not isinstance(value, bool):
                    yield value

            for name in ("viewport", "plot", "points", "paths", "arrows", "circles", "arcs",
                         "labels", "constructedPoints", "tool"):
                for value in numbers(drawing.get(name, {})):
                    self.assertTrue(math.isfinite(value), (name, value))
                    self.assertLessEqual(abs(value), 2**31 - 1, (name, value))
            for path in drawing["paths"]:
                for endpoint in (path["start"], path["end"]):
                    self.assertTrue(-.1 <= endpoint["x"] <= 850.1, path)
                    self.assertTrue(-.1 <= endpoint["y"] <= 600.1, path)

        away = self.draw(spec, extreme)
        assert_safe(away)
        for marker in away["constructedPoints"]:
            center = marker["center"]
            self.assertFalse(0 <= center["x"] <= 850 and 0 <= center["y"] <= 600,
                             "An offscreen endpoint wrapped into a false visible hit target")
        # A saved camera at the far endpoint must make that endpoint accessible
        # again. Panning then moves its actual dot/hit target by the gesture's
        # pixels without rebasing or rounding the stored mathematical point.
        restored = copy.deepcopy(extreme)
        restored["interactionDraft"]["camera"].update(x=far_position[0], y=far_position[1])
        visible = self.draw(spec, restored)
        assert_safe(visible)
        first = next(item["center"] for item in visible["constructedPoints"] if item["index"] == 0)
        self.assertAlmostEqual(425, first["x"], delta=.05)
        self.assertAlmostEqual(300, first["y"], delta=.05)
        self.assertNotIn("M", [item["text"] for item in visible["labels"]],
                         "A distant given point acquired a false onscreen label")
        moved = self.draw(spec, visible["state"], {"type": "view", "previous": point(300, 300),
                                                  "current": point(317, 287)})
        assert_safe(moved)
        last = next(item["center"] for item in moved["constructedPoints"] if item["index"] == 0)
        self.assertAlmostEqual(first["x"] + 17, last["x"], delta=.05)
        self.assertAlmostEqual(first["y"] - 13, last["y"], delta=.05)
        self.assertFalse(self.evaluate(spec, moved["state"])["correct"])

    def test_17_normal_gestures_at_geometry_limits_preserve_the_previous_attempt(self):
        spec = chain((3,))
        for sign in (-1, 1):
            with self.subTest(boundary=sign):
                draft = {"mode": "view", "activePoint": 0,
                         "camera": {"x": sign * 1e9, "y": sign * 1e9, "zoom": 1 / 64}}
                previous = self.draw(spec, graph([(3, 0)], draft))["state"]
                panned = self.draw(spec, previous, {"type": "view", "previous": point(300, 300),
                                                   "current": point(-sign * 1e6, sign * 1e6)})
                self.assertTrue(panned["gestureLimited"])
                self.assertEqual(previous["points"], panned["state"]["points"])
                for coordinate in ("x", "y"):
                    self.assertLessEqual(abs(panned["state"]["interactionDraft"]["camera"][coordinate]), 1e9)
                beyond = point(sign * 1e6, -sign * 1e6)
                moved = self.draw(spec, previous, {"type": "move_point", "index": 0, "current": beyond})
                self.assertTrue(moved["gestureLimited"])
                self.assertEqual(previous["points"], moved["state"]["points"])
                self.assertTrue(self.evaluate(spec, moved["state"])["correct"])
                empty = graph([], draft)
                placed = self.draw(spec, empty, {"type": "place", "index": 0, "current": beyond})
                self.assertTrue(placed["gestureLimited"])
                self.assertEqual([], placed["state"]["points"])
                holding = copy.deepcopy(previous)
                holding["interactionDraft"]["tool"] = {"x": sign * 1e9, "y": sign * 1e9,
                                                        "rotation": .31, "length": 3}
                tool = self.draw(spec, holding, {"type": "move_tool", "previous": point(300, 300),
                                                "current": beyond})
                self.assertTrue(tool["gestureLimited"])
                self.assertEqual(holding["interactionDraft"]["tool"], tool["state"]["interactionDraft"]["tool"])
                self.assertEqual(previous["points"], tool["state"]["points"])
                for delta in (-1e9, 1e9):
                    zoomed = self.draw(spec, previous, {"type": "zoom", "current": point(425, 300),
                                                        "delta": delta})
                    camera = zoomed["state"]["interactionDraft"]["camera"]
                    self.assertTrue(1 / 64 <= camera["zoom"] <= 1e12)
                    self.assertLessEqual(max(abs(camera["x"]), abs(camera["y"])), 1e9)
                    self.assertEqual(previous["points"], zoomed["state"]["points"])
        valid = graph([(3, 0)])
        original = copy.deepcopy(valid)
        self.fatal("math_editor", action="segment_layout", config=spec, state=valid,
                   width=850, height=600, gesture={"type": "view", "previous": point(300, 300),
                                                  "current": {"x": "not a pointer", "y": 300}})
        self.assertEqual(original, valid)
        self.assertTrue(self.evaluate(spec, valid)["correct"])

    def test_18_every_offered_span_can_be_captured_and_reopened_without_truncation(self):
        spec = chain()
        for points in ([(1e9, 0), (-1e9, 0)], [(1e9, 1e9), (-1e9, -1e9)]):
            with self.subTest(points=points):
                previous = self.draw(spec, graph(points))
                self.assertIn("part:1", [span["id"] for span in previous["measurements"]])
                measured = self.draw(spec, previous["state"], {"type": "measure", "id": "part:1"})
                opening = measured["state"]["interactionDraft"]["tool"]["length"]
                self.assertAlmostEqual(math.dist(*points), opening, delta=.001)
                self.assertGreater(opening, 1e9)
                restored = self.draw(spec, json.loads(json.dumps(measured["state"])))
                self.assertEqual(measured["state"], restored["state"])
                self.assertEqual(graph(points)["points"], restored["state"]["points"])
                # Correcting the measured span does not silently shorten the
                # captured rigid copy or poison the now-correct construction.
                corrected = copy.deepcopy(restored["state"])
                corrected["points"] = graph([(3, 0), (8, 0)])["points"]
                frozen = self.draw(spec, corrected)
                self.assertEqual(opening, frozen["state"]["interactionDraft"]["tool"]["length"])
                self.assertTrue(self.evaluate(spec, frozen["state"])["correct"])
        impossible = graph([(3, 0), (8, 0)], {"tool": {"x": 0, "y": 0, "rotation": 0,
                                                      "length": math.hypot(2e9, 2e9) * 1.01}})
        self.fatal("math_editor", action="segment_layout", config=spec, state=impossible,
                   width=850, height=600)


if __name__ == "__main__":
    unittest.main(verbosity=2)
