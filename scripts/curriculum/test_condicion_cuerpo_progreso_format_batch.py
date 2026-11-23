"""U4L1 formatting keeps authentic sources, retired editors and stored owners."""
from pathlib import Path
import unittest

from format_batch_history import FormatBatch, Publication


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "repeticion_y_terminacion/lesson_condicion_cuerpo_y_progreso.json"
)
PUBLICATIONS = {
    "before": Publication(
        "66dc8c6f50c42adc32bccd9a6a700d14e87abb84",
        "f52aef236ba3212a55dd09b9db5c2935b035b23d5d2a902f38b0c04e6cfa61c5"),
    "formatted": Publication(
        "f843a46e7c8d3b43dfb190e814cc5ca59ea944bc",
        "c03b79b794393200d354282a7628eee6b75576197f1ba7eb0f3580c4ded88292"),
    "retirement_parent": Publication(
        "2ca32b2337178d1effa3a2c81759e2493cd746d0",
        "c03b79b794393200d354282a7628eee6b75576197f1ba7eb0f3580c4ded88292"),
    "retirement": Publication(
        "286a2c7d9e383d4a377ba5c379dc8d6e9f95e766",
        "3bd250f4f4ec3fcf1309c3cc00cc7292f1e9a9d3deb546ea184e293ca0b0a27f"),
}
OWNERS = (
    "block_0000_0003", "block_0000_0005", "block_0001_0003",
    "block_0001_0004", "block_0002_0003", "block_0002_0004",
    "block_0003_0004", "block_0004_0002", "block_0004_0006",
    "block_0005_0002", "block_0005_0006",
)
RETIRED_OWNERS = {
    "block_0004_0002", "block_0004_0006", "block_0005_0002",
    "block_0005_0006",
}


class WhileConditionFormatBatch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.batch = FormatBatch(ROOT, RELATIVE, PUBLICATIONS, OWNERS, RETIRED_OWNERS)

    def test_all_originals_and_historical_responses_survive(self):
        self.batch.assert_history(self)

    def test_modified_sources_oracles_owners_and_history_are_rejected(self):
        self.batch.assert_negative_controls(self)

    def test_cpp23_original_and_spaced_loops_have_identical_output(self):
        self.batch.assert_msvc_outputs(self)


if __name__ == "__main__":
    unittest.main()

