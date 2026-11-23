"""U3L4: alternativas conservan fuentes históricas tras reordenar y retirar."""
from pathlib import Path
import unittest

from format_batch_history import FormatBatch, Publication


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "expresiones_y_decisiones/lesson_alternativas_y_fronteras.json"
)
PUBLICATIONS = {
    "before": Publication(
        "b0702ad2d1e2a4c421de9ad57ecb398f509ea91b",
        "8d7f619f9dac3b6dfb39e63123cbe587244e42264310da185630d0aee6cab3b6",
    ),
    "formatted": Publication(
        "3decb5af436bcd0a63525273246d1b9afbb23ed3",
        "8fb1dcd0f381205dc20a1a8138edf1ea11fd5091b0d9a7a9349cac1cf48095b6",
    ),
    "retirement_parent": Publication(
        "2ca32b2337178d1effa3a2c81759e2493cd746d0",
        "8fb1dcd0f381205dc20a1a8138edf1ea11fd5091b0d9a7a9349cac1cf48095b6",
    ),
    "retirement": Publication(
        "286a2c7d9e383d4a377ba5c379dc8d6e9f95e766",
        "3766c8922c794668d8f6090235ffad9e17d73fcdde613c8038da24fae3d63f59",
    ),
}
OWNERS = (
    "block_0000_0002",
    "block_0001_0002",
    "block_0002_0002",
    "block_0005_0002",
    "block_0006_0002",
)
RETIRED_OWNERS = {
    "block_0003_0002",
    "block_0003_0007",
    "block_0003_0010",
    "block_0006_0002",
}


class RealAlternativeFormatBatch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.batch = FormatBatch(ROOT, RELATIVE, PUBLICATIONS, OWNERS, RETIRED_OWNERS)

    def test_publicaciones_completas_formato_y_retiradas_autenticas(self):
        self.batch.assert_history(self)

    def test_rechaza_cambios_de_logica_oraculos_identidad_y_procedencia(self):
        self.batch.assert_negative_controls(self)

    def test_cpp23_fuentes_y_casos_publicados_conservan_resultados(self):
        self.batch.assert_msvc_outputs(self)


if __name__ == "__main__":
    unittest.main()

