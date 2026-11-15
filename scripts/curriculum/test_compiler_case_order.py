"""General authenticated case permutations preserve owners, evidence and exact cases."""
from copy import deepcopy
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import compiler_case_order_fixture_source as fixtures
import stable_state_layout as layout
import validar_json_programacion as curricular


class CompilerCaseOrderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before = fixtures.published()
        cls.owner = fixtures.OWNER
        cls.order = [21, *range(21)]
        cls.after = layout.reorder_compiler_cases(cls.before, cls.owner, cls.order)
        cls.manifest, cls.payloads = fixtures.build_fixtures()

    def reject(self, action):
        with self.assertRaises((ValueError, KeyError, TypeError, IndexError)):
            action()

    def test_exact_real_source_pins_and_closed_predecessor(self):
        for commit in (fixtures.CURRENT_COMMIT, fixtures.ORIGINAL_COMMIT):
            self.assertEqual(hashlib.sha256(fixtures.historical_bytes(commit)).hexdigest(),
                             fixtures.SOURCE_DIGESTS[commit])
        old = fixtures.published(fixtures.ORIGINAL_COMMIT)
        archive = next(item['block'] for item in self.before['stateLayout']['compilerRevisions']
                       if item['id'] == fixtures.EXPANDED_OWNER)
        self.assertEqual(layout._blocks(old)[fixtures.EXPANDED_OWNER], archive)
        self.assertEqual(len(archive['outputCheck']['cases']), 19)
        self.assertEqual(len(layout._blocks(self.before)[fixtures.EXPANDED_OWNER]['outputCheck']['cases']), 21)

    def test_real_six_movements_first_and_other_twenty_one_exact(self):
        old, new = layout._blocks(self.before)[self.owner], layout._blocks(self.after)[self.owner]
        self.assertEqual(len(new['outputCheck']['cases']), 22)
        self.assertEqual(new['outputCheck']['cases'], [old['outputCheck']['cases'][21], *old['outputCheck']['cases'][:21]])
        self.assertEqual(new['outputCheck']['cases'][0]['standardInput'], '3:0\n3:3\n0:3\n0:3\n3:3\n1:0')
        self.assertEqual(new['outputCheck']['cases'][0]['outputs'][0],
            'Aceptada | Ocupadas: 6\nAceptada | Ocupadas: 6\nAceptada | Ocupadas: 3\n'
            'Aceptada | Ocupadas: 0\nAceptada | Ocupadas: 0\nAceptada | Ocupadas: 1\nAceptadas: 6\nOcupadas: 1\n')
        self.assertEqual({k:v for k,v in old.items() if k!='outputCheck'},
                         {k:v for k,v in new.items() if k!='outputCheck'})
        self.assertNotEqual(layout.state_fingerprint(old), layout.state_fingerprint(new))
        self.assertEqual(layout.validate(self.before), layout.validate(self.after))

    def test_owner_family_maps_sources_dependencies_and_other_blocks_unchanged(self):
        for key in ('origin','family','current','additions','compilerRevisions','sourceFormatRevisions'):
            self.assertEqual(self.before['stateLayout'][key], self.after['stateLayout'][key])
        for key in ('referenceAfter','activityAfter','compilerPuzzlesRequired'):
            self.assertEqual(self.before.get(key), self.after.get(key))
        for identity, block in layout._all_blocks(self.before).items():
            if identity != self.owner:
                self.assertEqual(block, layout._all_blocks(self.after)[identity])
        self.assertEqual(self.before, fixtures.published())

    def test_three_exact_authored_fields_follow_case_twenty_one_to_zero(self):
        before = [f for f in self.before['textSemantics']['fields'] if f['block']==self.owner]
        after = [f for f in self.after['textSemantics']['fields'] if f['block']==self.owner]
        self.assertEqual(len(before), 3)
        expected = deepcopy(before)
        for field in expected:
            self.assertIn('/outputCheck/cases/21/', field['path'])
            field['path'] = field['path'].replace('/cases/21/', '/cases/0/')
        self.assertEqual(after, expected)
        self.assertEqual(self.after['stateLayout']['compilerCaseOrderRevisions'][0]['semanticBefore'], before)
        self.assertEqual([f for f in self.before['textSemantics']['fields'] if f['block']!=self.owner],
                         [f for f in self.after['textSemantics']['fields'] if f['block']!=self.owner])

    def test_same_text_distinct_semantics_are_moved_without_inference(self):
        before = fixtures.synthetic_publication()
        after = layout.reorder_compiler_cases(before, fixtures.SYNTHETIC_OWNER, [2,0,1])
        old = layout.text_semantics.validate(before)[fixtures.SYNTHETIC_OWNER]
        new = layout.text_semantics.validate(after)[fixtures.SYNTHETIC_OWNER]
        for new_index, old_index in enumerate((2,0,1)):
            for suffix in ('explanation','reviewFeedback/0/text'):
                self.assertEqual(new[f'/outputCheck/cases/{new_index}/{suffix}'],
                                 old[f'/outputCheck/cases/{old_index}/{suffix}'])
        self.assertNotEqual(new['/outputCheck/cases/0/explanation']['spans'],
                            new['/outputCheck/cases/1/explanation']['spans'])

    def test_multiple_permutations_preserve_full_append_only_history(self):
        order = [1,2,0,*range(3,22)]
        after = layout.reorder_compiler_cases(self.after, self.owner, order)
        layout.validate_transition(self.before, after)
        self.assertEqual(after['stateLayout']['compilerCaseOrderRevisions'][:1],
                         self.after['stateLayout']['compilerCaseOrderRevisions'])
        self.assertEqual(after['stateLayout']['editorial'][:len(self.after['stateLayout']['editorial'])],
                         self.after['stateLayout']['editorial'])
        self.assertEqual(after['stateLayout']['compilerCaseOrderRevisions'][1]['block'],
                         layout._blocks(self.after)[self.owner])
        expected = [self.order[i] for i in order]
        self.assertEqual(layout._blocks(after)[self.owner]['outputCheck']['cases'],
                         [layout._blocks(self.before)[self.owner]['outputCheck']['cases'][i] for i in expected])

    def test_return_requires_authentic_earlier_full_block_and_retains_both_records(self):
        inverse = [*range(1,22),0]
        self.reject(lambda: layout.reorder_compiler_cases(self.after,self.owner,inverse))
        self.reject(lambda: layout.reorder_compiler_cases(self.after,self.owner,inverse,return_to=0))
        self.reject(lambda: layout.reorder_compiler_cases(self.after,self.owner,inverse,return_to=True))
        returned = layout.reorder_compiler_cases(self.after,self.owner,inverse,return_to=1)
        layout.validate_transition(self.before, returned)
        self.assertEqual(layout._blocks(returned)[self.owner], layout._blocks(self.before)[self.owner])
        self.assertEqual(len(returned['stateLayout']['compilerCaseOrderRevisions']), 2)
        self.assertEqual(returned['stateLayout']['editorial'][-1]['returnTo']['revision'], 1)
        self.assertEqual(returned['textSemantics'], self.before['textSemantics'])

    def test_index_bijection_types_and_nonidentity_are_required(self):
        for order in (list(range(22)), list(range(21)), [21,*range(20),20,20],
                      [22,*range(21)], [-1,*range(21)], [21,False,*range(1,21)],
                      [21,0.0,*range(1,21)], '21,0,1', None):
            with self.subTest(order=order):
                self.reject(lambda: layout.reorder_compiler_cases(self.before,self.owner,order))

    def test_ordinary_compiler_fingerprint_is_not_relaxed_without_archive(self):
        forged = deepcopy(self.before)
        block = layout._blocks(forged)[self.owner]
        block['outputCheck']['cases'] = [block['outputCheck']['cases'][i] for i in self.order]
        self.reject(lambda: layout.validate(forged))
        forged['stateLayout']['editorial'].append({'id':self.owner,
            'beforeSha256':layout.fingerprint(layout._blocks(self.before)[self.owner]),
            'afterSha256':layout.fingerprint(block)})
        self.reject(lambda: layout.validate(forged))

    def test_false_unpublished_predecessor_cannot_pass_transition(self):
        before = fixtures.synthetic_publication(marked=False)
        unpublished = deepcopy(before)
        layout._blocks(unpublished)[fixtures.SYNTHETIC_OWNER]['outputCheck']['cases'][0]['explanation'] = 'Guía nunca publicada.'
        # A standalone source cannot prove external publication. The contextual
        # check must authenticate the exact predecessor supplied by Git.
        forged = layout.reorder_compiler_cases(unpublished, fixtures.SYNTHETIC_OWNER, [2,0,1])
        layout.validate(forged)
        self.reject(lambda: layout.validate_transition(before, forged))

    def test_semantic_loss_or_invention_during_permutation_is_rejected_contextually(self):
        lost = deepcopy(self.after)
        lost['textSemantics']['fields'] = [f for f in lost['textSemantics']['fields']
                                         if not (f['block']==self.owner and f['path']=='/outputCheck/cases/0/explanation')]
        layout.validate(lost)
        self.reject(lambda: layout.validate_transition(self.before,lost))
        before = fixtures.synthetic_publication(marked=False)
        after = layout.reorder_compiler_cases(before,fixtures.SYNTHETIC_OWNER,[2,0,1])
        invented = deepcopy(after)
        invented['textSemantics'] = {'version':1,'fields':[layout.text_semantics.make_field(
            invented, fixtures.SYNTHETIC_OWNER, '/outputCheck/cases/0/explanation', [])]}
        layout.validate(invented)
        self.reject(lambda: layout.validate_transition(before,invented))
        layout.validate_transition(after,invented)  # A separate later authorship remains valid.
        archive_lost = deepcopy(self.after)
        archive_lost['stateLayout']['compilerCaseOrderRevisions'][0]['semanticBefore'].pop()
        layout.validate(archive_lost)
        self.reject(lambda: layout.validate_transition(self.before,archive_lost))

    def test_later_same_text_semantic_repainting_remains_supported(self):
        before = layout.reorder_compiler_cases(fixtures.synthetic_publication(),fixtures.SYNTHETIC_OWNER,[2,0,1])
        after = deepcopy(before)
        field = next(f for f in after['textSemantics']['fields']
                     if f['path']=='/outputCheck/cases/0/explanation')
        field['spans'][0]['role'], field['spans'][0]['reading'] = 'cpp','technical'
        layout.validate_transition(before,after)
        self.assertEqual(before['pages'],after['pages'])
        self.assertEqual(before['stateLayout'],after['stateLayout'])

    def test_later_guidance_and_other_authored_question_remain_compatible(self):
        before = layout.reorder_compiler_cases(fixtures.synthetic_publication(),fixtures.SYNTHETIC_OWNER,[2,0,1])
        guided = fixtures.revised_guidance(before,fixtures.SYNTHETIC_OWNER)
        layout.validate_transition(before,guided)
        revised_question = layout.revise_ordinary_editorial(guided,'block_0000_0000',
            {'question':{'text':'Elige la opción que aparece primero.','spans':[]}})
        layout.validate_transition(guided,revised_question)
        self.assertEqual(before['stateLayout']['compilerCaseOrderRevisions'],
                         revised_question['stateLayout']['compilerCaseOrderRevisions'])

    def test_archives_and_editorial_prefix_cannot_be_rewritten(self):
        twice = layout.reorder_compiler_cases(self.after,self.owner,[1,2,0,*range(3,22)])
        for mutate in (
            lambda d: d['stateLayout']['compilerCaseOrderRevisions'].reverse(),
            lambda d: d['stateLayout']['compilerCaseOrderRevisions'].pop(0),
            lambda d: d['stateLayout']['editorial'].pop(0),
            lambda d: d['stateLayout']['compilerCaseOrderRevisions'][0].__setitem__('historySha256','0'*64),
        ):
            bad = deepcopy(twice)
            mutate(bad)
            self.reject(lambda: layout.validate_transition(self.after,bad))

    def test_format_at_either_side_preserves_source_and_case_identity(self):
        plain = fixtures.synthetic_publication()
        formatted = fixtures.formatted(plain,fixtures.SYNTHETIC_OWNER)
        after_format = layout.reorder_compiler_cases(formatted,fixtures.SYNTHETIC_OWNER,[2,0,1])
        order_first = layout.reorder_compiler_cases(plain,fixtures.SYNTHETIC_OWNER,[2,0,1])
        format_last = fixtures.formatted(order_first,fixtures.SYNTHETIC_OWNER)
        layout.validate_transition(formatted,after_format)
        layout.validate_transition(order_first,format_last)
        layout.validate_transition(plain,format_last)
        self.assertEqual(layout._blocks(after_format)[fixtures.SYNTHETIC_OWNER],
                         layout._blocks(format_last)[fixtures.SYNTHETIC_OWNER])

    def test_other_owner_retirement_preserves_exact_historical_order_and_marks(self):
        plain = fixtures.synthetic_publication()
        after = layout.reorder_compiler_cases(plain,fixtures.SYNTHETIC_OWNER,[2,0,1])
        retired = layout.retire(after,[fixtures.SYNTHETIC_NOTE])
        layout.validate_transition(after,retired)
        snapshot = retired['stateLayout']['retirementHistory'][-1]
        self.assertEqual(snapshot['pages'],after['pages'])
        self.assertEqual(snapshot['textSemantics'],after['textSemantics'])
        before_retired = layout.retire(plain,[fixtures.SYNTHETIC_NOTE])
        later_order = layout.reorder_compiler_cases(before_retired,fixtures.SYNTHETIC_OWNER,[2,0,1])
        self.assertEqual(later_order['stateLayout']['retirementHistory'],before_retired['stateLayout']['retirementHistory'])
        self.reject(lambda: layout.retire(after,[fixtures.SYNTHETIC_OWNER]))
        self.reject(lambda: layout.reorder_compiler_cases(before_retired,fixtures.SYNTHETIC_NOTE,[1,0]))

    def test_preexisting_closed_expansion_is_preserved_and_not_replaced(self):
        after = layout.reorder_compiler_cases(self.before,fixtures.EXPANDED_OWNER,[20,*range(20)])
        layout.validate_transition(self.before,after)
        self.assertEqual(after['stateLayout']['compilerRevisions'],self.before['stateLayout']['compilerRevisions'])
        self.assertEqual(layout.original_projection(after)['pages'],layout.original_projection(self.before)['pages'])
        self.assertEqual(layout._blocks(after)[fixtures.EXPANDED_OWNER]['outputCheck']['cases'][0],
                         layout._blocks(self.before)[fixtures.EXPANDED_OWNER]['outputCheck']['cases'][20])

    def test_curricular_alignment_keeps_previous_positions_across_multiple_publications(self):
        twice = layout.reorder_compiler_cases(self.after,self.owner,[1,2,0,*range(3,22)])
        for before,after in ((self.before,self.after),(self.after,twice),(self.before,twice)):
            aligned = layout.align_for_previous_validation(before,after)
            position = next((p,b) for p,page in enumerate(before['stateLayout']['current'])
                            for b,owner in enumerate(page['blocks']) if owner==self.owner)
            historic = layout.validate(before)[position]
            old = layout._blocks(before)[self.owner]
            new = aligned['pages'][position[0]]['blocks'][position[1]]
            self.assertEqual(old,new)
            self.assertTrue(curricular.preserves_previous_compiler(fixtures.ASSET,*historic,old,new,
                                                                  layout.original_projection(before)))

    def test_cmake_target_and_full_source_files_follow_existing_contract(self):
        before = fixtures.synthetic_publication(project=True)
        after = layout.reorder_compiler_cases(before,fixtures.SYNTHETIC_OWNER,[1,2,0])
        old,new = layout._blocks(before)[fixtures.SYNTHETIC_OWNER],layout._blocks(after)[fixtures.SYNTHETIC_OWNER]
        self.assertEqual(old['projectFiles'],new['projectFiles'])
        self.assertEqual(old['cmake'],new['cmake'])
        self.assertEqual(new['outputCheck']['cases'][0]['target'],'beta')
        self.assertEqual(new['outputCheck']['cases'],[old['outputCheck']['cases'][i] for i in (1,2,0)])

    def test_all_shared_negatives_fail_standalone_and_later_edges_keep_first_proof(self):
        for entry in self.manifest['invalid']:
            with self.subTest(name=entry['name']):
                bad=json.loads(self.payloads[entry['file']])
                self.reject(lambda: layout.validate(bad))
                if entry['name']=='floating-exit-code':
                    self.assertIs(type(layout._blocks(bad)[fixtures.OWNER]['outputCheck']['cases'][0]['exitCode']),float)
                    with self.assertRaisesRegex(ValueError,'integer type'):
                        layout.validate_transition(self.before,bad)
                if entry['name'].startswith('later-'):
                    record=bad['stateLayout']['compilerCaseOrderRevisions'][-1]
                    proof=bad['stateLayout']['editorial'][record['editorialIndex']]
                    self.assertEqual(proof['beforeSha256'],layout.fingerprint(record['block']))
                    self.assertEqual(proof['afterSha256'],layout.fingerprint(layout._compiler_case_order_after(record)))

    def test_shared_fixture_manifest_pins_real_bytes_and_all_sources_validate(self):
        self.assertEqual(self.payloads['u11l7.before.json'],fixtures.historical_bytes(fixtures.CURRENT_COMMIT))
        self.assertEqual(self.payloads['u11l7.original19.json'],fixtures.historical_bytes(fixtures.ORIGINAL_COMMIT))
        self.assertEqual(len(self.manifest['cases']),12)
        self.assertEqual(len(self.manifest['invalid']),35)
        for entry in self.manifest['cases']:
            for filename in [entry['before'],entry['after'],*(h['source'] for h in entry['history'])]:
                layout.validate(json.loads(self.payloads[filename]))
            self.assertEqual(entry['history'][-1]['source'],entry['after'])

    def test_cli_authors_only_a_new_output_and_refuses_overwrite(self):
        before=fixtures.synthetic_publication()
        with tempfile.TemporaryDirectory(prefix='case-order-cli-') as directory:
            root=Path(directory)
            source,order,output=root/'source.json',root/'order.json',root/'new.json'
            source.write_text(json.dumps(before,ensure_ascii=False),encoding='utf-8')
            order.write_text('[2,0,1]',encoding='utf-8')
            original_bytes=source.read_bytes()
            arguments=['stable_state_layout.py','reorder-compiler-cases',str(source),'--previous',str(source),
                       '--identity',fixtures.SYNTHETIC_OWNER,'--case-order',str(order),'--output',str(output)]
            with patch.object(sys,'argv',arguments),redirect_stdout(io.StringIO()):
                layout.main()
            result=json.loads(output.read_text(encoding='utf-8'))
            layout.validate_transition(before,result)
            self.assertEqual(source.read_bytes(),original_bytes)
            generated=output.read_bytes()
            with patch.object(sys,'argv',arguments),redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                layout.main()
            self.assertEqual(output.read_bytes(),generated)


if __name__ == '__main__':
    unittest.main()
