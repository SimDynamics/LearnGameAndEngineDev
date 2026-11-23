"""U3L6: cortocircuito conserva código, salidas y contratos archivados."""
from pathlib import Path
import unittest

from format_batch_history import FormatBatch, Publication


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "expresiones_y_decisiones/lesson_validez_y_cortocircuito.json"
)
PUBLICATIONS = {
    "before": Publication(
        "eed3ded9c5283fefe51e583c1c3bf2cebc25aed0",
        "93186ec0b048997670ab7fae5a40c89102970c3808efba254b7e8fd35965b33e",
    ),
    "formatted": Publication(
        "436156b25b1f13f0669d8f6b1567e6a3fcb7d551",
        "7f34c23510c0815a8e6bd3e5ef3c9fabb9245061b6fcbe9d089cef18869de661",
    ),
    "retirement_parent": Publication(
        "2ca32b2337178d1effa3a2c81759e2493cd746d0",
        "7f34c23510c0815a8e6bd3e5ef3c9fabb9245061b6fcbe9d089cef18869de661",
    ),
    "retirement": Publication(
        "286a2c7d9e383d4a377ba5c379dc8d6e9f95e766",
        "3c61e0450cac07edd6ed284a6003e535e571716fea28bb06f1601f47a9adaea5",
    ),
}
OWNERS = (
    "block_0000_0002",
    "block_0001_0002",
    "block_0002_0002",
    "block_0003_0002",
    "block_0003_0007",
    "block_0004_0002",
    "block_0006_0002",
    "cero_y_totales_validos_editor",
)
RETIRED_OWNERS = {
    "block_0003_0002",
    "block_0003_0007",
    "block_0006_0002",
    "cero_y_totales_validos_editor",
}


class ValidezCortocircuitoFormatoReal(unittest.TestCase):
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

