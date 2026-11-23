"""U4L2 sources keep authentic format and one-line order publications."""
from pathlib import Path
import unittest

import stable_state_layout as state
from format_batch_history import (
    FormatBatch, Publication, active_owners, assert_stable_storage, load_publication,
)
from test_one_line_order_cards import (
    IDENTITY as ORDER_IDENTITY, ORDER_BEFORE, ORDER_REFINED,
    assert_order_publication,
)


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "repeticion_y_terminacion/lesson_for_y_limites_del_recorrido.json"
)
PUBLICATIONS = {
    "before": Publication(
        "f843a46e7c8d3b43dfb190e814cc5ca59ea944bc",
        "712b646b1dea58f7549ea417f95f4f6c4f3976e1263616a07cacf8f10d78b3c4"),
    "formatted": Publication(
        "3efc5aec9122102e30ad1e3e1f607d4d604b2353",
        "8c45ce7f05fa77e0aace8df46534255567f8ff34f48597bf6ecb20ebcfd6b044"),
    "retirement_parent": Publication(
        "2ca32b2337178d1effa3a2c81759e2493cd746d0",
        "e7d437b1eedf69f4c7c6c3474ba6005f38f13dfc5051de1c023f9e1e90117cbd"),
    "retirement": Publication(
        "286a2c7d9e383d4a377ba5c379dc8d6e9f95e766",
        "cd728c11a4bf41c287c4759125fc8e2e7ce6ff50c1af03211c93a9c2a52dd660"),
}
OWNERS = (
    "block_0000_0002", "block_0000_0004", "block_0001_0003",
    "block_0001_0004", "block_0001_0005", "block_0002_0001",
    "block_0002_0002", "block_0003_0003", "block_0003_0004",
    "block_0004_0003", "block_0004_0004", "block_0005_0001",
    "block_0005_0003", "block_0005_0006", "block_0007_0002",
)
RETIRED_OWNERS = {"block_0005_0003", "block_0005_0006", "block_0007_0002"}


class ForLimitsFormatBatch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.batch = FormatBatch(ROOT, RELATIVE, PUBLICATIONS, OWNERS, RETIRED_OWNERS)
        cls.order_before = load_publication(ROOT, RELATIVE, ORDER_BEFORE)
        cls.order_refined = load_publication(ROOT, RELATIVE, ORDER_REFINED)

    def test_originals_and_order_separators_preserve_historical_state(self):
        self.batch.assert_history(self)
        state.validate_transition(self.batch.formatted, self.order_before)
        state.validate_transition(self.order_before, self.order_refined)
        state.validate_transition(self.order_refined, self.batch.retirement_parent)
        for document in (self.order_refined, self.batch.retirement, self.batch.current):
            assert_order_publication(self, self.order_before, self.order_refined, document)
            assert_stable_storage(self, self.order_before, document, (ORDER_IDENTITY,))
        original = active_owners(self.order_before)[0][ORDER_IDENTITY]
        self.assertEqual(5, len(original["fragments"]))

    def test_modified_sources_oracles_owners_and_history_are_rejected(self):
        self.batch.assert_negative_controls(self)

    def test_cpp23_sources_and_student_challenges_behave_identically(self):
        # The archived first challenge starts at 1; its required answer starts
        # at 2. Formatting must preserve that deliberate incorrect program.
        self.batch.assert_msvc_outputs(self, incorrect_initial={"block_0005_0003"})


if __name__ == "__main__":
    unittest.main()

