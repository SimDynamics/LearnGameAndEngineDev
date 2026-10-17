import hashlib
import json
import secrets
import socket
import ssl
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from uuid import uuid4

import httpx
import jwt
import pytest
from cryptography.hazmat.primitives.asymmetric import rsa
from fastapi.testclient import TestClient

from manage import make_pki, write_private
from servertec import CAPACITIES, HOUR_MS, Database, DomainError, FirebaseVerifier, PerPeerLimit, Settings, create_app


@pytest.fixture
def database(tmp_path):
    clock = [1_800_000_000_000]
    db = Database(tmp_path / 'runtime' / 'test.db', clock=lambda: clock[0])
    return db, clock


@pytest.fixture
def client(tmp_path, database):
    token = secrets.token_urlsafe(48)
    settings = Settings(tmp_path / 'runtime', testing_token_hash=hashlib.sha256(token.encode()).hexdigest())
    app = create_app(settings, database[0])
    with TestClient(app, base_url='https://127.0.0.1', client=('127.0.0.1', 55000)) as c:
        c.headers['Authorization'] = 'Bearer ' + token
        yield c


def test_initial_balance_only_once_and_reopen(database):
    db, clock = database
    assert db.hearts('u', 'programming')['available'] == 8
    db.hearts('u', 'programming', str(uuid4()))
    reopened = Database(db.path, clock=lambda: clock[0])
    assert reopened.hearts('u', 'programming')['available'] == 7


def test_accounts_and_routes_do_not_share_balances(database):
    db, _ = database
    db.hearts('a', 'programming', str(uuid4()))
    assert db.hearts('b', 'programming')['available'] == 8
    assert db.hearts('a', 'math_physics')['available'] == 12


def test_same_operation_is_debited_once_even_concurrently(database):
    db, _ = database
    op = str(uuid4())
    with ThreadPoolExecutor(max_workers=10) as pool:
        responses = list(pool.map(lambda _: db.hearts('u', 'programming', op), range(20)))
    assert sum(not r['replayed'] for r in responses) == 1
    assert db.hearts('u', 'programming')['available'] == 7


def test_idempotency_key_cannot_change_route(database):
    db, _ = database
    op = str(uuid4())
    db.hearts('u', 'programming', op)
    with pytest.raises(DomainError):
        db.hearts('u', 'math_physics', op)


def test_empty_receipt_cannot_charge_later_on_retry(database):
    db, clock = database
    for _ in range(8):
        assert db.hearts('u', 'programming', str(uuid4()))['status'] == 'charged'
    op = str(uuid4())
    assert db.hearts('u', 'programming', op)['status'] == 'empty'
    clock[0] += HOUR_MS
    assert db.hearts('u', 'programming', op)['status'] == 'empty'
    assert db.hearts('u', 'programming')['available'] == 1


def test_recovery_hourly_clamped_and_clock_rollback(database):
    db, clock = database
    db.hearts('u', 'programming', str(uuid4()))
    clock[0] -= HOUR_MS * 5
    assert db.hearts('u', 'programming')['available'] == 7
    clock[0] += HOUR_MS * 5 + HOUR_MS - 1
    assert db.hearts('u', 'programming')['available'] == 7
    clock[0] += 1
    assert db.hearts('u', 'programming')['available'] == 8
    clock[0] += HOUR_MS * 1000
    assert db.hearts('u', 'programming')['available'] == 8


def test_progress_idempotent_only_unit_or_course(database):
    db, clock = database
    db.complete('a', 'unit', 'cpp1', 'u1')
    clock[0] += 100
    db.complete('a', 'unit', 'cpp1', 'u1')
    assert len(db.progress('a')['completed']) == 1
    assert db.progress('b') == {'completed': []}
    with pytest.raises(DomainError):
        db.complete('a', 'lesson', 'cpp1', 'l1')


def test_sqlite_backup_includes_committed_wal(database, tmp_path):
    db, clock = database
    db.hearts('u', 'programming', str(uuid4()))
    backup = tmp_path / 'backup.db'
    db.backup(backup)
    assert Database(backup, clock=lambda: clock[0]).hearts('u', 'programming')['available'] == 7
    with pytest.raises(FileExistsError):
        db.backup(backup)


def test_health_does_not_leak_paths_or_secrets(client):
    client.headers.pop('Authorization')
    reply = client.get('/health')
    assert reply.status_code == 200
    assert reply.json() == {'status': 'ok', 'service': 'ServerTec'}
    assert reply.headers['cache-control'] == 'no-store'


@pytest.mark.parametrize('route', ['/v1/progress', '/v1/hearts/programming'])
def test_anonymous_requests_cannot_read_personal_data(client, route):
    client.headers.pop('Authorization')
    assert client.get(route).status_code == 401
    client.headers['X-User-Id'] = 'someone-else'
    assert client.get(route).status_code == 401


def test_unknown_route_rejected(client):
    assert client.get('/v1/hearts/unknown').status_code == 422


def test_fake_premium_and_client_balance_are_rejected(client):
    reply = client.post('/v1/hearts/debits', json={'route': 'programming', 'operation_id': str(uuid4()), 'premium': True, 'balance': 999})
    assert reply.status_code == 422
    assert client.get('/v1/hearts/programming').json()['available'] == 8


def test_https_required_and_no_swagger(client):
    assert client.get('http://127.0.0.1/health').status_code == 400
    assert client.get('/docs').status_code == 404
    assert client.get('/openapi.json').status_code == 404


def test_oversized_body_rejected(client):
    assert client.post('/v1/hearts/debits', content=b'x' * 17000).status_code == 413


def test_invalid_completion_and_sql_payload_rejected(client):
    for item in ['../u1', "'; DROP TABLE hearts;--", 'x' * 129]:
        assert client.put('/v1/progress/completions', json={'kind': 'unit', 'course': 'cpp1', 'item': item}).status_code == 422
    assert client.put('/v1/progress/completions', json={'kind': 'lesson', 'course': 'cpp1', 'item': 'l1'}).status_code == 422
    assert client.put('/v1/progress/completions', json={'kind': 'unit', 'course': 'cpp1', 'item': 'u1'}).status_code == 200


def test_api_replay_receipt(client):
    payload = {'route': 'programming', 'operation_id': str(uuid4())}
    assert client.post('/v1/hearts/debits', json=payload).json()['available'] == 7
    assert client.post('/v1/hearts/debits', json=payload).json()['replayed']
    assert client.get('/v1/hearts/programming').json()['available'] == 7


def test_testing_token_rejected_from_remote_peer(tmp_path, database):
    token = secrets.token_urlsafe(48)
    settings = Settings(tmp_path, testing_token_hash=hashlib.sha256(token.encode()).hexdigest())
    app = create_app(settings, database[0])
    with TestClient(app, base_url='https://127.0.0.1', client=('198.51.100.10', 45000)) as c:
        assert c.get('/v1/progress', headers={'Authorization': 'Bearer ' + token, 'X-Forwarded-For': '127.0.0.1'}).status_code == 401


def test_testing_credentials_cannot_bind_to_network(tmp_path):
    with pytest.raises(ValueError):
        Settings(tmp_path, host='0.0.0.0', testing_token_hash='a' * 64).validate()
    with pytest.raises(ValueError):
        Settings(tmp_path, testing=False).validate()


def test_runtime_in_source_forbidden():
    with pytest.raises(ValueError):
        Settings(Path(__file__).parent / 'runtime', testing_token_hash='a' * 64).validate()


def test_limiter_is_bounded():
    limiter = PerPeerLimit(2)
    assert limiter.allow('a') and limiter.allow('a')
    assert not limiter.allow('a')
    for i in range(11000):
        limiter.allow(str(i))
    assert len(limiter.entries) == 10000


def signed_token(key, **changes):
    now = int(time.time())
    payload = {'sub': 'google-user', 'iss': 'https://securetoken.google.com/test-project',
               'aud': 'test-project', 'iat': now, 'exp': now + 3600, 'auth_time': now,
               'firebase': {'sign_in_provider': 'google.com'}}
    payload.update(changes)
    return jwt.encode(payload, key, algorithm='RS256', headers={'kid': 'test-key'})


def test_real_jwt_crypto_verification_without_network():
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    verifier = FirebaseVerifier('test-project')
    verifier._key = lambda _: key.public_key()
    assert verifier.verify(signed_token(key)) == 'google-user'
    for changes in [{'aud': 'wrong-project'}, {'iss': 'https://attacker.example'},
                    {'exp': 1000}, {'auth_time': int(time.time()) + 600},
                    {'firebase': {'sign_in_provider': 'anonymous'}}, {'sub': ''}]:
        with pytest.raises((ValueError, jwt.PyJWTError)):
            verifier.verify(signed_token(key, **changes))
    other_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    with pytest.raises(jwt.InvalidSignatureError):
        verifier.verify(signed_token(other_key))


def test_none_algorithm_rejected():
    verifier = FirebaseVerifier('test-project')
    with pytest.raises(ValueError):
        verifier.verify(jwt.encode({'sub': 'admin'}, key=None, algorithm='none'))


def test_actual_tls_server_persistence_and_hostname_validation(tmp_path):
    # Genuine sockets, certificates, Uvicorn and SQLite. No mocked transport.
    state = tmp_path / 'state'
    state.mkdir(mode=0o700)
    make_pki(state, b'testing-only-strong-password-47', [])
    token = secrets.token_urlsafe(48)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    write_private(state / 'config.json', json.dumps(dict(testing=True, project_id='',
        testing_token_hash=hashlib.sha256(token.encode()).hexdigest(), host='127.0.0.1', port=port)).encode())
    output = tmp_path / 'server.log'
    with output.open('wb') as log:
        process = subprocess.Popen([sys.executable, 'manage.py', 'serve', '--state', str(state)],
                                   cwd=Path(__file__).parent, stdout=log, stderr=subprocess.STDOUT)
        try:
            ctx = ssl.create_default_context(cafile=str(state / 'authority.pem'))
            with httpx.Client(verify=ctx, trust_env=False, timeout=1, base_url=f'https://127.0.0.1:{port}') as client:
                for _ in range(100):
                    try:
                        reply = client.get('/health')
                        break
                    except httpx.TransportError:
                        if process.poll() is not None:
                            pytest.fail(output.read_text())
                        time.sleep(0.05)
                else:
                    pytest.fail('TLS server never started')
                assert reply.status_code == 200
                assert 'server' not in reply.headers
                assert client.get('/v1/progress').status_code == 401
                client.headers['Authorization'] = 'Bearer ' + token
                body = {'operation_id': str(uuid4()), 'route': 'programming'}
                assert client.post('/v1/hearts/debits', json=body).json()['available'] == 7
                assert client.post('/v1/hearts/debits', json=body).json()['replayed']
            with pytest.raises(ssl.SSLCertVerificationError):
                with socket.create_connection(('127.0.0.1', port), timeout=2) as plain:
                    ctx.wrap_socket(plain, server_hostname='wrong-server.invalid')
            with pytest.raises(httpx.ConnectError):
                httpx.get(f'https://127.0.0.1:{port}/health', verify=True, trust_env=False)
        finally:
            process.terminate()
            process.wait(timeout=10)
    assert Database(state / 'servertec.db').hearts('local-test-user', 'programming')['available'] == 7
    assert token not in output.read_text()
