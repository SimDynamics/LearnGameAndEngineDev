"""Local administration. Never exposes an admin HTTP endpoint."""
from __future__ import annotations
import argparse
import getpass
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import secrets
import ssl
import sys
from datetime import datetime, timedelta, timezone
import urllib.request

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID
from servertec import Database, Settings, create_app


def state_default() -> Path:
    root = Path(os.environ.get("LOCALAPPDATA", Path.home() / ".local" / "share"))
    return root / "ServerTec" / "state"


def write_private(path: Path, content: bytes):
    # O_EXCL prevents silently overwriting a certificate/database/secret.
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as stream:
        stream.write(content)


def make_pki(root: Path, password: bytes, addresses: list[str]):
    if len(password) < 16:
        raise ValueError("La frase de la autoridad debe tener al menos 16 caracteres")
    now = datetime.now(timezone.utc)
    ca_key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "ServerTec private test authority")])
    ca = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
          .public_key(ca_key.public_key()).serial_number(x509.random_serial_number())
          .not_valid_before(now - timedelta(minutes=5)).not_valid_after(now + timedelta(days=3650))
          .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
          .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), critical=True)
          .add_extension(x509.SubjectKeyIdentifier.from_public_key(ca_key.public_key()), critical=False)
          .sign(ca_key, hashes.SHA256()))
    write_private(root / "authority-key.pem", ca_key.private_bytes(serialization.Encoding.PEM,
                  serialization.PrivateFormat.PKCS8, serialization.BestAvailableEncryption(password)))
    write_private(root / "authority.pem", ca.public_bytes(serialization.Encoding.PEM))
    issue_leaf(root, ca_key, ca, addresses)


def issue_leaf(root: Path, ca_key, ca, addresses: list[str]):
    # Loopback names enable local and ADB-reverse TLS tests without disabling validation.
    ips = sorted(set(["127.0.0.1", "::1", *addresses]))
    parsed = [ipaddress.ip_address(value) for value in ips]
    key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    now = datetime.now(timezone.utc)
    cert = (x509.CertificateBuilder()
            .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "ServerTec")]))
            .issuer_name(ca.subject).public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - timedelta(minutes=5)).not_valid_after(now + timedelta(days=90))
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .add_extension(x509.KeyUsage(True, False, True, False, False, False, False, False, False), critical=True)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
            .add_extension(x509.SubjectAlternativeName([x509.DNSName("localhost"), *[x509.IPAddress(ip) for ip in parsed]]), critical=False)
            .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()), critical=False)
            .sign(ca_key, hashes.SHA256()))
    write_private(root / "server-key.pem", key.private_bytes(serialization.Encoding.PEM,
                  serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    write_private(root / "server.pem", cert.public_bytes(serialization.Encoding.PEM))


def load_settings(state: Path) -> Settings:
    data = json.loads((state / "config.json").read_text(encoding="utf-8"))
    settings = Settings(state=state, **data)
    settings.validate()
    return settings


def main():
    parser = argparse.ArgumentParser(description="ServerTec: administracion local, sin abrir firewall")
    parser.add_argument("action", choices=["init", "serve", "verify", "backup", "certificate-info"])
    parser.add_argument("--state", type=Path, default=state_default())
    args = parser.parse_args()
    state = args.state.resolve()
    if state.is_relative_to(Path(__file__).resolve().parent):
        raise ValueError("Usa una carpeta de estado FUERA del repositorio")
    if args.action == "init":
        if state.exists() and any(state.iterdir()):
            raise ValueError("La carpeta de estado ya contiene datos. No se sobrescribe nada")
        password = getpass.getpass("Frase privada para proteger la CA (minimo 16 caracteres): ")
        if password != getpass.getpass("Repite la frase: "):
            raise ValueError("Las frases no coinciden")
        if len(password.encode()) < 16:
            raise ValueError("Frase demasiado corta")
        state.mkdir(mode=0o700, parents=True, exist_ok=True)
        make_pki(state, password.encode(), [])
        token = secrets.token_urlsafe(48)
        write_private(state / "testing-token.txt", token.encode())
        config = dict(testing=True, project_id="", testing_token_hash=hashlib.sha256(token.encode()).hexdigest(),
                      host="127.0.0.1", port=8443)
        write_private(state / "config.json", json.dumps(config, indent=2).encode())
        Database(state / "servertec.db")
        print("Inicializado en modo de prueba local. Claves privadas fuera del codigo.")
        print("No se modificaron certificados de Windows, router ni firewall.")
    elif args.action == "serve":
        settings = load_settings(state)
        if not ipaddress.ip_address(settings.host).is_loopback:
            raise ValueError("Esta primera entrega solo permite loopback. Exposicion de red pendiente de revision")
        import uvicorn
        config = uvicorn.Config(create_app(settings), host=settings.host, port=settings.port,
                                ssl_certfile=str(state / "server.pem"), ssl_keyfile=str(state / "server-key.pem"),
                                ssl_version=ssl.PROTOCOL_TLS_SERVER, proxy_headers=False,
                                server_header=False, access_log=False, workers=1,
                                limit_concurrency=64, timeout_keep_alive=5, log_level="warning")
        config.load()
        config.ssl.minimum_version = ssl.TLSVersion.TLSv1_2
        print(f"ServerTec de prueba: https://{settings.host}:{settings.port}/health", flush=True)
        print("No sirve Internet ni Google Play todavia. Ctrl+C para detener.", flush=True)
        uvicorn.Server(config).run()
    elif args.action == "verify":
        settings = load_settings(state)
        if not ipaddress.ip_address(settings.host).is_loopback:
            raise ValueError("La verificacion inicial se limita a loopback")
        host = f"[{settings.host}]" if ":" in settings.host else settings.host
        context = ssl.create_default_context(cafile=str(state / "authority.pem"))
        with urllib.request.urlopen(f"https://{host}:{settings.port}/health", context=context, timeout=8) as reply:
            if json.load(reply) != {"status": "ok", "service": "ServerTec"}:
                raise ValueError("Respuesta inesperada")
        print("HTTPS OK: cadena del certificado y nombre/IP verificados, sin excepciones inseguras.")
    elif args.action == "backup":
        load_settings(state)
        folder = state / "backups"
        folder.mkdir(mode=0o700, exist_ok=True)
        target = folder / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + secrets.token_hex(4) + ".db")
        Database(state / "servertec.db").backup(target)
        print("Copia SQLite verificada:", target.name)
        print("Esta copia esta en el MISMO equipo. Falta una copia externa para tolerar averias.")
    else:
        cert = x509.load_pem_x509_certificate((state / "server.pem").read_bytes())
        ca = x509.load_pem_x509_certificate((state / "authority.pem").read_bytes())
        print(json.dumps({"expires_utc": cert.not_valid_after_utc.isoformat(),
                          "ca_sha256": ca.fingerprint(hashes.SHA256()).hex(),
                          "addresses": [str(v) for v in cert.extensions.get_extension_for_class(x509.SubjectAlternativeName).value]}, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, FileNotFoundError, FileExistsError, OSError) as exc:
        print(f"No se completo la operacion: {exc}", file=sys.stderr)
        sys.exit(1)
