"""Self-contained loopback HTTPS acceptance test; never uses the student's state.

No cloud calls, ADB commands, firewall changes or permanent certificate installs.
The random test credential is sent only to the newly generated local TLS server.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import secrets
import socket
import sqlite3
import ssl
import subprocess
import sys
import tempfile
import time
from uuid import uuid4

import httpx

from manage import make_pki, write_private


class SelfCheckError(RuntimeError):
    """Messages are fixed labels: no tokens, addresses, paths or response bodies."""


def ensure(condition: bool, code: str) -> None:
    if not condition:
        raise SelfCheckError(code)


def stop_owned_process(process: subprocess.Popen) -> None:
    """Only stop the child we created, never kill by process name or TCP port."""
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def protect_temporary_state(path: Path) -> None:
    if os.name == 'nt':
        # Temporary files hold only disposable test credentials, still restrict ACLs.
        windows = Path(os.environ['SystemRoot']) / 'System32'
        result = subprocess.run([str(windows / 'whoami.exe'), '/user', '/fo', 'csv', '/nh'],
                                capture_output=True, text=True, check=True, timeout=10)
        import csv
        row = next(csv.reader(result.stdout.splitlines()))
        sid = row[-1].strip()
        import re
        ensure(re.fullmatch(r'S-1-[0-9-]+', sid) is not None, 'windows_identity_unavailable')
        result = subprocess.run([str(windows / 'icacls.exe'), str(path), '/inheritance:r',
                                 '/grant:r', f'*{sid}:(OI)(CI)F', '*S-1-5-18:(OI)(CI)F',
                                 '*S-1-5-32-544:(OI)(CI)F'],
                                capture_output=True, check=False, timeout=10)
        ensure(result.returncode == 0, 'temporary_permissions_failed')
    else:
        path.chmod(0o700)


def private_tls_context(ca_file: Path) -> ssl.SSLContext:
    # Construct explicitly so SSLKEYLOGFILE cannot export even temporary TLS keys.
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.load_verify_locations(cafile=str(ca_file))
    ctx.verify_flags |= ssl.VERIFY_X509_STRICT
    ensure(ctx.check_hostname and ctx.verify_mode == ssl.CERT_REQUIRED,
           'certificate_validation_not_enabled')
    return ctx


def run_selfcheck(*, timeout: float = 30.0) -> dict:
    ensure(1 <= timeout <= 120, 'invalid_startup_timeout')
    checks: list[str] = []
    with tempfile.TemporaryDirectory(prefix='ServerTec-QA-') as folder:
        state = Path(folder).resolve()
        ensure(not state.is_relative_to(Path(__file__).resolve().parent), 'unsafe_temp_location')
        protect_temporary_state(state)
        make_pki(state, secrets.token_urlsafe(32).encode(), [])
        token = secrets.token_urlsafe(48)
        # Ephemeral port: no need to stop the ordinary server using 8443.
        # A bind race fails closed: the peer must present OUR temporary CA.
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        write_private(state / 'config.json', json.dumps(dict(
            testing=True, project_id='', host='127.0.0.1', port=port,
            testing_token_hash=hashlib.sha256(token.encode()).hexdigest())).encode())
        env = os.environ.copy()
        env.pop('SSLKEYLOGFILE', None)
        env['PYTHONUNBUFFERED'] = '1'
        with (state / 'qa-server.log').open('wb') as log:
            process = subprocess.Popen(
                [sys.executable, str(Path(__file__).with_name('manage.py')),
                 'serve', '--state', str(state)],
                cwd=Path(__file__).parent, env=env,
                stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
            try:
                ctx = private_tls_context(state / 'authority.pem')
                with httpx.Client(base_url=f'https://127.0.0.1:{port}', verify=ctx,
                                  trust_env=False, follow_redirects=False, timeout=4) as client:
                    deadline = time.monotonic() + timeout
                    while True:
                        ensure(process.poll() is None, 'local_server_stopped_before_ready')
                        try:
                            health = client.get('/health')
                            break
                        except httpx.TransportError:
                            ensure(time.monotonic() < deadline, 'https_startup_timeout')
                            time.sleep(0.05)
                    ensure(health.status_code == 200 and health.json() ==
                           {'status': 'ok', 'service': 'ServerTec'}, 'unexpected_health_response')
                    checks.append('https_con_certificado_y_nombre_validos')
                    ensure(client.get('/v1/progress').status_code == 401, 'anonymous_request_accepted')
                    checks.append('rechazo_sin_autenticacion')
                    client.headers['Authorization'] = 'Bearer ' + token
                    ensure(client.get('/v1/hearts/programming').json()['available'] == 8,
                           'initial_balance_incorrect')
                    operation = {'operation_id': str(uuid4()), 'route': 'programming'}
                    first = client.post('/v1/hearts/debits', json=operation)
                    second = client.post('/v1/hearts/debits', json=operation)
                    ensure(first.status_code == second.status_code == 200 and
                           first.json()['status'] == 'charged' and first.json()['available'] == 7 and
                           not first.json()['replayed'] and second.json()['replayed'] and
                           second.json()['available'] == 7, 'duplicate_debit_not_protected')
                    checks.append('descuento_unico_ante_reintento')
                    ensure(client.get('/v1/hearts/math_physics').json()['available'] == 12,
                           'routes_not_isolated')
                    checks.append('saldos_separados_por_ruta')
                    conflict = client.post('/v1/hearts/debits', json={
                        **operation, 'route': 'math_physics'})
                    ensure(conflict.status_code == 409, 'changed_operation_not_rejected')
                    checks.append('rechazo_de_uuid_reutilizado_en_otra_ruta')
                    completion = {'kind': 'unit', 'course': 'qa_course', 'item': 'qa_unit'}
                    for _ in range(2):
                        ensure(client.put('/v1/progress/completions', json=completion).status_code == 200,
                               'unit_completion_failed')
                    progress = client.get('/v1/progress').json()['completed']
                    ensure(len(progress) == 1 and progress[0]['item'] == 'qa_unit',
                           'duplicate_progress_not_protected')
                    course = {'kind': 'course', 'course': 'qa_course', 'item': 'qa_course'}
                    ensure(client.put('/v1/progress/completions', json=course).status_code == 200,
                           'course_completion_failed')
                    ensure(len(client.get('/v1/progress').json()['completed']) == 2,
                           'course_progress_missing')
                    ensure(client.put('/v1/progress/completions', json={
                        **completion, 'kind': 'lesson'}).status_code == 422, 'lesson_sync_accepted')
                    checks.append('unidades_y_cursos_sin_sincronizar_lecciones')
                    fake = client.post('/v1/hearts/debits', json={
                        'operation_id': str(uuid4()), 'route': 'programming', 'premium': True})
                    ensure(fake.status_code == 422, 'fake_premium_accepted')
                    checks.append('premium_no_activable_por_el_cliente')
                    # Send no credential to the two deliberately untrusted TLS sessions.
                    rejected_ca = False
                    try:
                        with httpx.Client(verify=True, trust_env=False, timeout=3) as untrusted:
                            untrusted.get(f'https://127.0.0.1:{port}/health')
                    except httpx.ConnectError:
                        rejected_ca = True
                    ensure(rejected_ca, 'untrusted_ca_accepted')
                    rejected_name = False
                    try:
                        with socket.create_connection(('127.0.0.1', port), timeout=3) as plain:
                            with ctx.wrap_socket(plain, server_hostname='wrong-server.invalid'):
                                pass
                    except ssl.SSLCertVerificationError:
                        rejected_name = True
                    ensure(rejected_name, 'wrong_certificate_name_accepted')
                    checks.append('rechazo_de_ca_y_nombre_incorrectos')
                ensure(process.poll() is None, 'server_stopped_during_checks')
            finally:
                stop_owned_process(process)
        # Read only the disposable database AFTER shutdown, including its WAL.
        con = sqlite3.connect(state / 'servertec.db')
        try:
            ensure(con.execute('PRAGMA integrity_check').fetchone()[0] == 'ok', 'database_integrity_failed')
            ensure(con.execute('SELECT COUNT(*) FROM receipts').fetchone()[0] == 1,
                   'persisted_receipt_count_incorrect')
            ensure(con.execute('SELECT COUNT(*) FROM completions').fetchone()[0] == 2,
                   'progress_not_persisted')
        finally:
            con.close()
        ensure(token not in (state / 'qa-server.log').read_text(errors='replace'), 'credential_in_logs')
        checks.append('persistencia_y_sin_credenciales_en_logs')
    # Successful return only after temporary key/database cleanup.
    return {'status': 'ok', 'checks_passed': len(checks), 'checks': checks,
            'production_state_modified': False, 'external_services_used': False,
            'temporary_state_removed': True, 'android_app_modified': False}


def main() -> int:
    try:
        result = run_selfcheck()
    except KeyboardInterrupt:
        print('AutoPrueba cancelada. No se publico el servidor en Internet.', file=sys.stderr)
        return 130
    except Exception as error:
        # No raw exception/body can copy credentials or personal paths into a shared summary.
        code = str(error) if isinstance(error, SelfCheckError) else type(error).__name__
        print('AutoPrueba NO aprobada: ' + code, file=sys.stderr)
        return 1
    print('=== AUTOPRUEBA PARA COMPARTIR ===')
    for check in result['checks']:
        print('[OK] ' + check)
    print('Resultado: ' + str(result['checks_passed']) + '/' + str(result['checks_passed']))
    print('Sin tocar datos reales, Android, router ni firewall. Estado temporal eliminado.')
    print('Esto NO verifica acceso publico, Google real, Honor ni LDPlayer.')
    print('=== FIN AUTOPRUEBA ===')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
