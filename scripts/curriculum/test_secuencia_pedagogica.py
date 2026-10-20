"""Regression cases for navigation dependencies, not a pedagogical certificate."""

import copy
import unittest

import verificar_secuencia_pedagogica as sequence


SOURCE = '#include <iostream>\nint main() { std::cout << "Listo"; }\n'


def lesson(*pages):
    return {"pages": [{"blocks": blocks} for blocks in pages]}


def paragraph(text):
    return {"type": "paragraph", "text": text}


def compiler(**fields):
    return dict({"type": "cpp_compiler", "initialCode": SOURCE}, **fields)


def prediction(question="Predice la salida de este programa.", **fields):
    return dict({"type": "output_prediction", "question": question, "code": SOURCE}, **fields)


class PedagogicalSequenceTest(unittest.TestCase):
    def codes(self, document, severity="ERROR"):
        return [item.code for item in sequence.review_lesson(document) if item.severity == severity]

    def test_direct_back_navigation_is_an_error(self):
        document = lesson([compiler()], [paragraph("Vuelve al editor de la página anterior y cambia el dato.")])
        self.assertEqual(["REGRESO_EDITOR_ANTERIOR"], self.codes(document))

    def test_code_editor_spelling_is_also_resolved(self):
        document = lesson([compiler()], [paragraph("Regresa al editor de código de la página anterior.")])
        self.assertEqual(["REGRESO_EDITOR_ANTERIOR"], self.codes(document))

    def test_conceptual_recall_does_not_request_navigation(self):
        self.assertEqual([], self.codes(lesson([paragraph("Recupera el concepto de copia de la página anterior.")])))

    def test_retries_of_same_objective_remain_available(self):
        document = lesson([paragraph("Si el resultado difiere, corrige el literal y vuelve a ejecutar hasta conseguir el mensaje solicitado."), compiler()])
        self.assertEqual([], self.codes(document))
        self.assertNotIn("POSIBLES_OBJETIVOS_MULTIPLES", self.codes(document, "AVISO"))

    def test_prediction_explicitly_linked_to_previous_editor_is_an_error(self):
        document = lesson([compiler(), prediction("Antes de ejecutar el programa inicial del editor, escribe su salida.")])
        self.assertEqual(["PREDICCION_DESPUES_EDITOR"], self.codes(document))

    def test_prediction_can_precede_the_editor(self):
        document = lesson([prediction("Antes de ejecutar el programa inicial del editor, escribe su salida."), compiler()])
        self.assertEqual([], self.codes(document))

    def test_independent_prediction_does_not_imply_a_backtrack(self):
        self.assertEqual([], self.codes(lesson([compiler(), prediction()])))

    def test_prediction_of_another_program_is_not_linked_by_appearance(self):
        document = lesson([compiler(), prediction("Antes de ejecutar en el editor, escribe la salida.", code="int main() {}")])
        self.assertEqual([], self.codes(document))

    def test_source_comparison_preserves_significant_spaces(self):
        document = lesson([compiler(), prediction("Antes de ejecutar el editor, predice.", code=SOURCE.replace('"Listo"', '" Listo"'))])
        self.assertEqual([], self.codes(document))

    def test_line_ending_differences_do_not_hide_the_same_program(self):
        document = lesson([compiler(), prediction("Antes de ejecutar el editor, predice.", code=SOURCE.replace("\n", "\r\n"))])
        self.assertEqual(["PREDICCION_DESPUES_EDITOR"], self.codes(document))

    def test_forward_response_before_interposed_editor_is_an_error(self):
        document = lesson([paragraph("Antes de ejecutar, resuelve la predicción del final de esta página."), compiler(), prediction()])
        self.assertEqual(["RESPUESTA_POSTERIOR_ANTES_EDITOR"], self.codes(document))

    def test_puzzle_already_before_editor_is_valid(self):
        document = lesson([paragraph("Antes de ejecutar, resuelve la predicción del final de esta página."), prediction(), compiler()])
        self.assertEqual([], self.codes(document))

    def test_future_puzzle_without_execution_dependency_is_not_an_error(self):
        document = lesson([paragraph("Después resolverás el puzzle del final de esta página."), compiler(), prediction()])
        self.assertEqual([], self.codes(document))

    def test_hidden_feedback_is_not_a_normal_instruction(self):
        document = lesson([prediction(explanation="Vuelve al editor de la página anterior.", answers=["Listo"])])
        self.assertEqual([], self.codes(document))

    def test_multiple_versions_are_review_warnings_not_a_claim_of_failure(self):
        document = lesson([paragraph("En una segunda versión cambia el literal y ejecuta."), compiler()])
        self.assertEqual([], self.codes(document))
        self.assertIn("POSIBLES_OBJETIVOS_MULTIPLES", self.codes(document, "AVISO"))

    def test_missing_contract_does_not_invent_an_output(self):
        document = lesson([compiler()])
        previous = copy.deepcopy(document)
        self.assertEqual(["EDITOR_SIN_CONTRATO"], self.codes(document, "AVISO"))
        self.assertEqual(previous, document)

    def test_declared_contract_is_not_required_to_match_prediction_answers(self):
        document = lesson([prediction(answers=["Listo"]), compiler(outputCheck={"cases": [{"standardInput": "", "outputs": ["Otro objetivo"]}]})])
        self.assertEqual([], self.codes(document))
        self.assertEqual([], self.codes(document, "AVISO"))

    def test_diagnostic_task_does_not_require_a_fake_successful_output(self):
        document = lesson([paragraph("Observa el diagnóstico: este código no compila."), compiler()])
        self.assertIn("OBJETIVO_DIAGNOSTICO", self.codes(document, "AVISO"))
        self.assertEqual([], self.codes(document))

    def test_locations_report_real_page_and_block(self):
        document = lesson([compiler()], [paragraph("Texto."), prediction("Antes de ejecutar el editor, predice.")])
        finding = next(item for item in sequence.review_lesson(document, "fixture") if item.code == "PREDICCION_DESPUES_EDITOR")
        self.assertEqual(("fixture", 2, 2, 1, 1), (finding.lesson, finding.page, finding.block, finding.related_page, finding.related_block))


if __name__ == "__main__":
    unittest.main()
