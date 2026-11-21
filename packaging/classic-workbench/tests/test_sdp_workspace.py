"""AI/SQL native workspace contracts; no application, network or credentials."""
from decimal import Decimal
import importlib
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'classic-workbench/modules/wb.utils'))
backend = importlib.import_module('sdp_workspace_backend')
Rows = importlib.import_module('sdp_tools_backend').Rows


class Result:
    def __init__(self, rows, columns):
        self.rows, self.columns, self.index = rows, [SimpleNamespace(name=name, columnType=kind) for name, kind in columns], 0
    def goToFirstRow(self):
        self.index = 0
        return bool(self.rows)
    def nextRow(self):
        self.index += 1
        return self.index < len(self.rows)
    def stringFieldValue(self, index):
        return self.rows[self.index][index]


class WorkspaceContractTests(unittest.TestCase):
    def test_definition_editor_rejects_nul_before_native_c_string_truncation(self):
        with self.assertRaisesRegex(ValueError,'NUL'):
            backend.definition_text('Behavior',dict(definition='ilk\0ikinci'))

    def test_prompt_cannot_escape_into_script(self):
        attack = "İptal'; DROP TABLE siparisler; --\\\0"
        source = backend.conversation_script('satış', 'yardım`cı', attack)
        self.assertNotIn('DROP TABLE', source)
        self.assertIn(attack.encode('utf-8').hex(), source)
        self.assertIn('START AI CONVERSATION `satış`.`yardım``cı`', source)
        self.assertTrue(source.endswith('COMMIT;\nFINISH;'))
        self.assertIn('APPEND RESPONSE;', source)
        self.assertIn('COMMIT ASYNC;', backend.conversation_script('db', 'c', 'prompt', True))

    def test_run_commands_keep_last_run_in_main_session_and_reject_bad_id(self):
        self.assertEqual(backend.run_statement('FETCH RESPONSE'), 'FETCH RESPONSE @sdp_ai_last_run_id;')
        with self.assertRaises(ValueError):
            backend.run_statement('CANCEL RUN', 'abc; DROP TABLE t')

    def test_catalog_document_preserves_json_null_and_numbers(self):
        value = dict(name='siparis_bilgisi', version=2, settings=None, parameters=[dict(name='id', type='BIGINT')])
        self.assertEqual(backend.document(Rows([(json.dumps(value),)], ['DOCUMENT'])), value)
        for result in [Rows(), Rows([(None,)], ['document']), Rows([('[]',)], ['document']), Rows([('x',)], ['id'])]:
            with self.assertRaises(ValueError):
                backend.document(result)

    def test_only_explicit_available_capability_is_enabled(self):
        for value in [None, 1, 'true', dict(available=1), dict(implemented=True), False]:
            self.assertFalse(backend.capability_enabled(dict(features=dict(ai_models_v1=value)), 'ai_models_v1'))
        self.assertTrue(backend.capability_enabled(dict(features=dict(ai_models_v1=dict(available=True))), 'ai_models_v1'))

    def test_parameter_types_are_exact_and_nullable_is_explicit(self):
        value = '1234567890123456789.123456789012345678'
        self.assertEqual(backend.typed_value(value, 'DECIMAL(38,18)'), Decimal(value))
        self.assertEqual(backend.typed_value(str((1 << 63) - 1), 'BIGINT'), (1 << 63) - 1)
        self.assertEqual(backend.typed_value('  metin  ', 'TEXT'), '  metin  ')
        self.assertIsNone(backend.typed_value('', 'TEXT', True, True))
        for args in [('nan', 'DECIMAL'), (str(1 << 63), 'BIGINT'), ('1; DROP', 'INT'), ('yes', 'BOOLEAN'), ('', 'TEXT', False, True)]:
            with self.assertRaises(ValueError):
                backend.typed_value(*args)


    def test_native_parameter_width_and_unsigned_boundaries(self):
        self.assertEqual(backend.typed_value(str((1 << 64) - 1), 'BIGINT UNSIGNED'), (1 << 64) - 1)
        self.assertEqual(backend.typed_value('255', 'TINYINT UNSIGNED'), 255)
        self.assertEqual(backend.typed_value('-8388608', 'MEDIUMINT'), -8388608)
        self.assertEqual(backend.typed_value('+32767', 'SMALLINT'), 32767)
        for value, kind in [('256','TINYINT UNSIGNED'),('-1','BIGINT UNSIGNED'),
                            ('32768','SMALLINT'),('8388608','MEDIUMINT'),
                            (str(1 << 64),'BIGINT UNSIGNED'),('-0.5','DECIMAL(8,2) UNSIGNED')]:
            with self.assertRaises(ValueError):
                backend.typed_value(value, kind)

    def test_knowledge_decimal_source_never_becomes_approximate_literal(self):
        source=backend.knowledge_call('db','k',[Decimal('1E+30'),Decimal('1E-28')])
        self.assertEqual(source,'EXECUTE KNOWLEDGE `db`.`k`(1000000000000000000000000000000, 0.0000000000000000000000000001);')
        with self.assertRaises(ValueError):
            backend.knowledge_call('db','k',[Decimal('1E+1000000')])

    def test_knowledge_edit_preserves_signature_and_changes_the_stored_object(self):
        doc = dict(parameters=[dict(name='p`id', type='BIGINT UNSIGNED', nullable=False)],
            result_columns=[dict(name='id', type='BIGINT', nullable=False),
                dict(name='note', type='VARCHAR(40) CHARACTER SET `utf8mb4` COLLATE `utf8mb4_bin`', nullable=True)])
        body = "SELECT id, CONCAT(note, ';') AS note FROM rows_table WHERE id=:p_id -- edited query"
        source = backend.object_edit_sql('Knowledge', 'satış', 'k`name', doc, body)
        self.assertTrue(source.startswith('ALTER KNOWLEDGE `satış`.`k``name`(`p``id` BIGINT UNSIGNED NOT NULL)'))
        self.assertIn('RETURNS TABLE (`id` BIGINT NOT NULL, `note` VARCHAR(40) CHARACTER SET `utf8mb4` COLLATE `utf8mb4_bin` NULL)', source)
        self.assertTrue(source.endswith('READ ONLY SQL SECURITY INVOKER\nAS\n'+body+'\n;'))
        self.assertEqual(doc['result_columns'][1]['nullable'], True)
        self.assertIn('ALTER KNOWLEDGE `db`.`k`()\nRETURNS TABLE (`id` INT NOT NULL)',
            backend.object_edit_sql('Knowledge', 'db', 'k', dict(parameters=[], result_columns=[dict(name='id',type='INT')]), 'SELECT 1 AS id'))

    def test_knowledge_edit_rejects_untrusted_type_source_and_invalid_metadata(self):
        for bad_type in ('BIGINT); DROP TABLE orders; --', 'INT DEFAULT (writing_function())',
                         'VARCHAR(40) CHARACTER SET `utf8mb4; DROP` COLLATE utf8mb4_bin',
                         'DECIMAL(65,66)', 'TIME(7)', 'JSON UNSIGNED'):
            with self.subTest(type=bad_type), self.assertRaises(ValueError):
                backend.object_edit_sql('Knowledge','db','k',dict(parameters=[], result_columns=[dict(name='id',type=bad_type)]),'SELECT 1')
        for doc in (dict(parameters=[], result_columns=[]), dict(parameters=None,result_columns=[]),
                    dict(parameters=[],result_columns=[dict(name='id',type='INT',nullable='false')]),
                    dict(parameters=[],result_columns=[dict(name='id',type='INT'),dict(name='ID',type='INT')])):
            with self.subTest(document=doc), self.assertRaises(ValueError):
                backend.object_edit_sql('Knowledge','db','k',doc,'SELECT 1')

    def test_behavior_edit_is_alter_and_text_cannot_become_sql(self):
        body = "Türkçe cevap ver.'; DELETE FROM orders; --\\\0"
        source = backend.object_edit_sql('Behavior','db','b`name',{},body)
        self.assertTrue(source.startswith('ALTER BEHAVIOR `db`.`b``name`\nAS '))
        self.assertNotIn('DELETE FROM',source)
        self.assertIn(body.encode().hex(),source)
        editable=body.removesuffix('\0')
        self.assertEqual(backend.definition_text('Behavior',dict(definition=editable)),editable)
        self.assertEqual(backend.definition_text('Knowledge',dict(definition="SELECT ';'")),"SELECT ';'")

    def test_response_schema_edit_uses_existing_dispatch_and_next_version(self):
        body = '{"type":"object","minimum":0.10000000000000000001,"title":"x\'; DROP TABLE orders; --"}'
        source = backend.object_edit_sql('Yanıt şemaları','satış','s`name',dict(version=7),body)
        self.assertTrue(source.startswith("CALL sdp_ai.dispatch('alter','response_schema',"))
        self.assertIn("JSON_OBJECT('version',8,'schema',CAST(",source)
        self.assertIn(body.encode().hex(),source)
        self.assertNotIn('DROP TABLE',source)
        self.assertNotIn('ALTER RESPONSE',source)
        self.assertTrue(source.endswith(' AS JSON)),@sdp_workspace_output);'))
        self.assertIn("'response_schema',DATABASE(),", backend.object_edit_sql('Yanıt şemaları','','s',dict(version=7),'{}'))
        for body in ('[]','null','true','{"minimum":NaN}','not json'):
            with self.subTest(body=body), self.assertRaises(ValueError):
                backend.object_edit_sql('Yanıt şemaları','db','s',dict(version=7),body)
        for version in (None, True, '7', 0, (1 << 64)-1):
            with self.subTest(version=version), self.assertRaises(ValueError):
                backend.object_edit_sql('Yanıt şemaları','db','s',dict(version=version),'{}')

    def test_definition_editor_json_preserves_decimal_numbers_and_types(self):
        doc = backend.document(Rows([('{"definition":{"minimum":0.10000000000000000001,"large":9007199254740993,"small":1e-20,"enabled":false,"nullable":null,"items":["Türkçe",2]}}',)], ['document']))
        text = backend.definition_text('Yanıt şemaları',doc)
        parsed = json.loads(text,parse_float=Decimal)
        self.assertEqual(parsed,doc['definition'])
        self.assertIn('"minimum": 0.10000000000000000001',text)
        self.assertIn('"small": 0.00000000000000000001',text)
        self.assertIs(parsed['enabled'],False)
        self.assertIsNone(parsed['nullable'])
        self.assertEqual(text,backend.definition_text('Yanıt şemaları',dict(definition=dict(reversed(list(doc['definition'].items()))))))

    def test_definition_editor_rejects_unsupported_or_unbounded_values(self):
        nested = {}
        for _ in range(66): nested = {'child':nested}
        for value in (dict(definition={'minimum':Decimal('NaN')}),dict(definition={'minimum':Decimal('1E+1000000')}),
                      dict(definition={'text':'x'*1048576}),dict(definition=nested),dict(definition=[])):
            with self.subTest(value_type=type(value['definition'])), self.assertRaises(ValueError):
                backend.definition_text('Yanıt şemaları',value)
        with self.assertRaises(ValueError):
            backend.object_edit_sql('AI modelleri','db','m',{},'definition')

    def test_permission_target_and_role_cannot_add_statement(self):
        source = backend.grants_sql('PROCEDURE', 'db', 'siparis_iptal', 'role`name', True, True, True)
        self.assertIn('TO ROLE `role``name`', source)
        self.assertIn('REQUIRE APPROVAL ON PROCEDURE `db`.`siparis_iptal`;', source)
        with self.assertRaises(ValueError):
            backend.grants_sql('KNOWLEDGE', 'db', 'k', 'r', approve=True)

    def test_each_object_grants_its_native_privilege(self):
        for kind, privilege in [('MODEL','USE'),('BEHAVIOR','USE'),('RESPONSE SCHEMA','USE'),
                                ('CONVERSATION','SELECT, APPEND'),('KNOWLEDGE','EXECUTE'),('FUNCTION','EXECUTE')]:
            self.assertTrue(backend.grants_sql(kind,'db','o','r',execute=True).startswith('GRANT '+privilege+' ON '+kind+' '))

    def test_explicit_tools_are_typed_targets_and_never_sql_source(self):
        source=backend.conversation_create('db','c','m',True,tools=[
            {'kind':'KNOWLEDGE','name':'lookup`orders'}, {'kind':'PROCEDURE','schema':'db','name':'siparis_iptal'}])
        self.assertIn('WITH FUNCTIONS (KNOWLEDGE `db`.`lookup``orders`, PROCEDURE `db`.`siparis_iptal`)',source)
        for tools in ([{'kind':'SQL','name':'DROP TABLE orders'}],
                      [{'kind':'FUNCTION','name':'f'},{'kind':'FUNCTION','name':'f'}], [{}]):
            with self.assertRaises(ValueError):backend.conversation_create('db','c','m',True,tools=tools)
        with self.assertRaises(ValueError):backend.conversation_create('db','c','m',False,tools=[{'kind':'FUNCTION','name':'f'}])

    def test_action_preparation_keeps_exact_parameters_outside_sql_source(self):
        attack="'); DELETE FROM orders; --"
        source=backend.action_prepare('db','siparis_iptal',json.dumps([9007199254740993,attack,None])[:-1]+', 1e30]')
        self.assertIn('9007199254740993',source)
        self.assertIn('1000000000000000000000000000000',source)
        self.assertNotIn('DELETE FROM',source)
        self.assertIn(attack.encode().hex(),source)
        self.assertIn('RETURNING ID INTO @sdp_action_id;',source)
        self.assertTrue(source.endswith('DESCRIBE ACTION @sdp_action_id;'))
        for value in ('{}','[NaN]','[[1]]','[{"sql":"CALL p()"}]','[1e10000000]'):
            with self.assertRaises(ValueError):backend.action_prepare('db','p',value)

    def test_action_commands_pin_the_inspected_id_and_reject_statement_injection(self):
        for verb in ('DESCRIBE','APPROVE','DENY','EXECUTE'):
            self.assertEqual(backend.action_statement(verb,'A'*32),verb+" ACTION '"+'a'*32+"';")
            with self.assertRaises(ValueError):backend.action_statement(verb,"';DROP TABLE orders;")
        self.assertIn("FOR TOOL CALL '"+'b'*32+"'",backend.tool_action_prepare('B'*32))
        with self.assertRaises(ValueError):backend.tool_action_prepare('')
        value=backend.document(Rows([('{"arguments":[1234567890123456789.123456789,9007199254740993]}',)],['document']))
        self.assertEqual(Decimal('1234567890123456789.123456789'),value['arguments'][0])
        self.assertEqual(9007199254740993,value['arguments'][1])

    def test_describe_uses_metadata_query_and_preserves_bigint_decimal(self):
        calls = []
        result = Result([('18446744073709551615', '999999999999999999.125', None)], [('id', 'int'), ('amount', 'numeric'), ('nullable', 'string')])
        editor = SimpleNamespace(executeManagementQuery=lambda source, flags: calls.append(source) or result)
        rows = backend.MetadataSession(editor).run_sql('DESCRIBE KNOWLEDGE `db`.`k`;')
        self.assertEqual(rows.rows, [(18446744073709551615, Decimal('999999999999999999.125'), None)])
        self.assertEqual(calls, ['DESCRIBE KNOWLEDGE `db`.`k`;'])

    def test_mutation_executes_on_main_connection_without_aux_command(self):
        calls = []
        editor = SimpleNamespace(executeScriptAndOutputToGrid=lambda source: calls.append(('main', source)) or 1)
        api = backend.WorkspaceBackend(editor)
        self.assertEqual(api.execute('COMMIT;\nFINISH;'), 1)
        self.assertEqual(calls, [('main', 'COMMIT;\nFINISH;')])

    def test_missing_capability_blocks_before_catalog_query(self):
        editor = SimpleNamespace()
        api = backend.WorkspaceBackend(editor)
        api.capabilities = dict(features=dict(ai_models_v1=False))
        with self.assertRaisesRegex(ValueError, 'ai_models_v1'):
            api.list_objects('AI modelleri')

    def test_main_query_path_keeps_execution_session(self):
        calls = []
        editor = SimpleNamespace(executeQuery=lambda sql, flags: calls.append(('main', sql)) or Result([], []))
        api = backend.WorkspaceBackend(editor)
        api.main.run_sql('SELECT @sdp_ai_last_run_id;')
        self.assertEqual(calls, [('main', 'SELECT @sdp_ai_last_run_id;')])

    def test_role_sensitive_catalog_uses_the_selected_sql_session(self):
        calls=[]
        def main(sql,flags):
            calls.append(sql)
            if sql.startswith('SHOW '):
                return Result([('role_visible','db')],[('name','string'),('schema_name','string')])
            return Result([('{"name":"role_visible","version":1}',)],[('document','string')])
        def auxiliary(sql,flags):
            raise AssertionError('An auxiliary session can have a different active role set')
        api=backend.WorkspaceBackend(SimpleNamespace(executeQuery=main,executeManagementQuery=auxiliary))
        api.capabilities=dict(features=dict(stored_knowledge_v1=True))
        self.assertEqual(api.list_objects('Knowledge').rows,[('role_visible','db')])
        self.assertEqual(api.describe('Knowledge','db','role_visible')['name'],'role_visible')
        self.assertEqual(calls,['SHOW KNOWLEDGES;','DESCRIBE KNOWLEDGE `db`.`role_visible`;'])


    def test_model_sql_carries_reference_and_never_credential_value(self):
        for provider in ('OPENAI', 'ANTHROPIC'):
            source = backend.model_registration('db', 'yardımcı', provider, 'remote-model', secret_env='SDP_KEY')
            self.assertIn('PROVIDER ' + provider, source)
            self.assertIn('secret_env = ', source)
            self.assertIn('5344505f4b4559', source)
            self.assertNotIn('endpoint = ', source)
        source = backend.model_registration('db', 'm', 'OPENAI', 'remote-model', credential_ref='my-provider')
        self.assertIn('credential_ref = ', source)
        with self.assertRaises(ValueError):
            backend.model_registration('db', 'm', 'OPENAI', 'model', secret_env='KEY', credential_ref='ref')

    def test_gguf_import_requires_relative_store_path_and_sha256(self):
        source = backend.model_registration('db', 'm', 'LOCAL_GGUF', artifact='models/model.gguf', sha256='a' * 64)
        self.assertIn('IMPORT MODEL `db`.`m` FROM FILE ', source)
        self.assertIn('context_tokens = 4096', source)
        for artifact in ['../model.gguf', '/model.gguf', 'C:\\models\\model.gguf', 'models/model.bin']:
            with self.assertRaises(ValueError):
                backend.model_registration('db', 'm', 'LOCAL_GGUF', artifact=artifact, sha256='a' * 64)
        for sha256 in ['', 'invalid', 'g' * 64]:
            with self.assertRaises(ValueError):
                backend.model_registration('db', 'm', 'LOCAL_GGUF', artifact='model.gguf', sha256=sha256)

    def test_worker_binding_is_owner_scoped_and_contains_only_secret_references(self):
        actor = r'ali\@team@localhost'
        fragment = backend.model_worker_binding(actor, 'OPENAI', credential_ref='my-key')
        self.assertEqual(fragment, {'credentials': {actor: {'my-key': {
            'source': 'windows', 'target': 'StarDataParadise/ai/my-key', 'providers': ['openai']}}}})
        fragment = backend.model_worker_binding(actor, 'ANTHROPIC', secret_env='CLAUDE_KEY',
            endpoint='https://proxy.example.com/messages')
        self.assertEqual(fragment['credentials'][actor]['CLAUDE_KEY'], {
            'source': 'environment', 'name': 'CLAUDE_KEY', 'providers': ['anthropic'],
            'endpoints': ['https://proxy.example.com/messages']})
        fragment = backend.model_worker_binding(actor, 'ANTHROPIC', secret_env='CLAUDE_KEY',
            endpoint='https://api.anthropic.com/v1/messages')
        self.assertNotIn('endpoints', fragment['credentials'][actor]['CLAUDE_KEY'])
        local = backend.model_worker_binding(actor, 'LOCAL_GGUF')
        self.assertIn(actor, local['local_runtime']['import_roots'])
        self.assertNotIn('credentials', local)
        for kwargs in ({'credential_ref': 'sk-raw-key'}, {'secret_env': 'KEY', 'credential_ref': 'ref'},
                       {'secret_env': 'KEY', 'endpoint': 'https://api.example.com?key=raw'}):
            with self.assertRaises(ValueError):
                backend.model_worker_binding(actor, 'OPENAI', **kwargs)

    def test_remote_endpoint_cannot_embed_api_key_or_cleartext_transport(self):
        for endpoint in ['http://api.example.com', 'https://user:secret@api.example.com', 'https://api.example.com/#key',
                         'https://api.example.com/?api_key=secret', 'https://api.example.com:invalid',
                         'https://api.example.com:0', 'https://api.example.com:65536', 'https://[invalid']:
            with self.assertRaises(ValueError):
                backend.model_registration('db', 'm', 'OPENAI', 'remote-model', secret_env='KEY', endpoint=endpoint)
        source = backend.model_registration('db', 'm', 'OPENAI', 'remote-model', secret_env='KEY', endpoint='https://api.example.com:443/v1')
        self.assertIn('endpoint = ', source)


if __name__ == '__main__':
    unittest.main()
