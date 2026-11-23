"""U3L7: formato conserva if independientes y asociación de cada else."""
from pathlib import Path
import unittest

from format_batch_history import FormatBatch, Publication, all_owners, source_field
import code_format_compat as source_format


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "expresiones_y_decisiones/lesson_decisiones_compuestas_y_pruebas.json"
)
PUBLICATIONS = {
    "before": Publication(
        "436156b25b1f13f0669d8f6b1567e6a3fcb7d551",
        "08035235647f3d4ad3e30d367a0322742f9bb6bfdcdba0e5eb605dee0943c72e",
    ),
    "formatted": Publication(
        "99e54029606bd0481481210cd43813314287e561",
        "f8887db7fd56152111dec477c527a3a277a5fef0bdbf17816202fdb97c8db6c8",
    ),
    "retirement_parent": Publication(
        "2ca32b2337178d1effa3a2c81759e2493cd746d0",
        "ef1e93c88c23fcd57b23bc9aa1405506e447cd662648225d1ecaa178e5b6eb6f",
    ),
    "retirement": Publication(
        "286a2c7d9e383d4a377ba5c379dc8d6e9f95e766",
        "760a26bcc9678ef868e4df718686a49914a66c3748df39613cf05d5ec5ab980b",
    ),
}
OWNERS = (
    "block_0000_0002",
    "block_0000_0004",
    "block_0001_0002",
    "block_0002_0002",
    "block_0003_0003",
    "block_0003_0008",
    "block_0004_0003",
    "block_0005_0001",
    "block_0006_0003",
    "block_0007_0002",
)
RETIRED_OWNERS = {
    "block_0003_0003",
    "block_0003_0008",
    "block_0007_0002",
}


class DecisionesCompuestasFormatoReal(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.batch = FormatBatch(ROOT, RELATIVE, PUBLICATIONS, OWNERS, RETIRED_OWNERS)

    def test_publicaciones_completas_formato_y_retiradas_autenticas(self):
        self.batch.assert_history(self)

    def test_rechaza_cambios_de_logica_oraculos_identidad_y_procedencia(self):
        self.batch.assert_negative_controls(self)

    def test_cpp23_fuentes_y_casos_publicados_conservan_resultados(self):
        self.batch.assert_msvc_outputs(self)

    def test_if_independientes_y_cada_else_conservan_su_asociacion(self):
        previous = all_owners(self.batch.before)
        for document in (self.batch.formatted, self.batch.retirement, self.batch.current):
            resolved = all_owners(document)
            independent = source_format._cpp_tokens(resolved["block_0001_0002"]["code"])
            self.assertEqual(2, independent.count("id:if"))
            self.assertNotIn("id:else", independent)
            self.assertIn(("op:}", "id:if"), tuple(zip(independent, independent[1:])))
            for owner in OWNERS:
                old, new = previous[owner], resolved[owner]
                # Full lexical equality includes braces, every condition and
                # the exact else/if association; spacing may continue to evolve.
                self.assertEqual(source_format._cpp_tokens(old[source_field(old)]),
                                 source_format._cpp_tokens(new[source_field(new)]), owner)


if __name__ == "__main__":
    unittest.main()

