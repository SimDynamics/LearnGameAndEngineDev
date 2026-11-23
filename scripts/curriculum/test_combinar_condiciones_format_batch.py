"""U3L5: formato de combinaciones con contratos históricos auténticos."""
from pathlib import Path
import unittest

from format_batch_history import FormatBatch, Publication


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "expresiones_y_decisiones/lesson_combinar_condiciones.json"
)
PUBLICATIONS = {
    "before": Publication(
        "fc7d0adb0487905a60a3f3f78419b1983058b20e",
        "92be987ccfeff11c7ffa28c86125ca042913144d020def7603141ff78e3eda91",
    ),
    "formatted": Publication(
        "eed3ded9c5283fefe51e583c1c3bf2cebc25aed0",
        "99281ac5d718e07bbfca1ce4631ad86c8e55ca15e3ec6cefc463f3df48fc1447",
    ),
    "retirement_parent": Publication(
        "2ca32b2337178d1effa3a2c81759e2493cd746d0",
        "99281ac5d718e07bbfca1ce4631ad86c8e55ca15e3ec6cefc463f3df48fc1447",
    ),
    "retirement": Publication(
        "286a2c7d9e383d4a377ba5c379dc8d6e9f95e766",
        "61e9dd673e6ba5cdc639ba68da42e32255dc0c42bd599e5651f803de833fdc05",
    ),
}
OWNERS = (
    "block_0000_0002",
    "block_0000_0004",
    "block_0001_0002",
    "block_0001_0003",
    "block_0002_0004",
    "block_0003_0002",
    "block_0003_0007",
    "block_0004_0001",
    "block_0005_0002",
    "combinaciones_faltantes_editor",
)
RETIRED_OWNERS = {
    "block_0003_0002",
    "block_0003_0007",
    "block_0005_0002",
    "combinaciones_faltantes_editor",
}


class CombinarCondicionesFormatoReal(unittest.TestCase):
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

