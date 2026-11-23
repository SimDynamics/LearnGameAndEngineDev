"""U3L3: fuentes publicadas y retiradas conservan sus propietarios."""
from pathlib import Path
import unittest

from format_batch_history import FormatBatch, Publication


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "expresiones_y_decisiones/lesson_comparaciones_y_condiciones.json"
)
PUBLICATIONS = {
    "before": Publication(
        "b1177700366ddbd05ebc3d8acf8b0cd9ea3c42f1",
        "8349e48538b70c656186c691e6e9ee8c2ad70958d98737cda77a86e21f86af59",
    ),
    "formatted": Publication(
        "66dc8c6f50c42adc32bccd9a6a700d14e87abb84",
        "1302b7574f5c4bc9911dea85afb8ca518bd31e8877d331ca30a4c70d1f381bfd",
    ),
    "retirement_parent": Publication(
        "2ca32b2337178d1effa3a2c81759e2493cd746d0",
        "1302b7574f5c4bc9911dea85afb8ca518bd31e8877d331ca30a4c70d1f381bfd",
    ),
    "retirement": Publication(
        "286a2c7d9e383d4a377ba5c379dc8d6e9f95e766",
        "4df21cb150c7e7eede752a43087f336396d1869d1a351bba10fa38b408b4b1dc",
    ),
}
OWNERS = (
    "block_0000_0002",
    "block_0000_0004",
    "block_0001_0003",
    "block_0002_0002",
    "block_0002_0004",
    "block_0002_0005",
    "block_0003_0003",
    "block_0003_0008",
    "block_0004_0001",
    "block_0005_0002",
)
RETIRED_OWNERS = {
    "block_0003_0003",
    "block_0003_0008",
    "block_0005_0002",
}


class ComparacionesFormatoReal(unittest.TestCase):
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

