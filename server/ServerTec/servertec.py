"""ServerTec: isolated self-hosted API. Not connected to the Android app yet."""
from __future__ import annotations

import asyncio
import hashlib
import hmac
import ipaddress
import json
import re
import sqlite3
import threading
import time
from collections import OrderedDict
from dataclasses import dataclass
from pathlib import Path
from typing import Literal
from uuid import UUID

import httpx
import jwt
from cryptography import x509
from fastapi import Depends, FastAPI, HTTPException, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from pydantic import BaseModel, ConfigDict, Field
from starlette.concurrency import run_in_threadpool

HOUR_MS = 3_600_000
CAPACITIES = {"programming": 8, "math_physics": 12}
KEYS_URL = "https://www.googleapis.com/robot/v1/metadata/x509/securetoken@system.gserviceaccount.com"


class DomainError(Exception):
    def __init__(self, code: str, status: int = 409):
        self.code, self.status = code, status


@dataclass(frozen=True)
class Settings:
    state: Path
    testing: bool = True
    project_id: str = ""
    testing_token_hash: str = ""
    host: str = "127.0.0.1"
    port: int = 8443

    def validate(self) -> None:
        address = ipaddress.ip_address(self.host)
        if not 1024 <= self.port <= 65535:
            raise ValueError("Use an unprivileged TCP port, 1024..65535")
        if self.testing:
            if not address.is_loopback or not re.fullmatch(r"[0-9a-f]{64}", self.testing_token_hash):
                raise ValueError("Testing credentials require loopback binding and a generated token")
        elif not re.fullmatch(r"[a-z][a-z0-9-]{4,62}[a-z0-9]", self.project_id):
            raise ValueError("Configure the real Firebase project ID before enabling network access")
        # Runtime data must not be saved under this source checkout.
        source = Path(__file__).resolve().parent
        if self.state.resolve().is_relative_to(source):
            raise ValueError("State, certificates and databases must be outside the source checkout")


class Database:
    def __init__(self, path: Path, clock=None):
        self.path = path
        self.clock = clock or (lambda: time.time_ns() // 1_000_000)
        path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        con = self.connect()
        try:
            con.executescript("""
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS schema_version(version INTEGER NOT NULL);
                INSERT INTO schema_version SELECT 1 WHERE NOT EXISTS(SELECT 1 FROM schema_version);
                CREATE TABLE IF NOT EXISTS hearts(
                    uid TEXT NOT NULL, route TEXT NOT NULL, credit_ms INTEGER NOT NULL,
                    anchor_ms INTEGER NOT NULL, revision INTEGER NOT NULL,
                    PRIMARY KEY(uid,route), CHECK(credit_ms>=0));
                CREATE TABLE IF NOT EXISTS receipts(
                    uid TEXT NOT NULL, operation TEXT NOT NULL, route TEXT NOT NULL,
                    reply TEXT NOT NULL, PRIMARY KEY(uid,operation));
                CREATE TABLE IF NOT EXISTS completions(
                    uid TEXT NOT NULL, kind TEXT NOT NULL, course TEXT NOT NULL,
                    item TEXT NOT NULL, completed_ms INTEGER NOT NULL,
                    PRIMARY KEY(uid,kind,course,item));
            """)
            if con.execute("SELECT version FROM schema_version").fetchall() != [(1,)]:
                raise RuntimeError("Unsupported database schema; do not overwrite it")
        finally:
            con.close()
        self.path.chmod(0o600)

    def connect(self):
        con = sqlite3.connect(self.path, timeout=10, isolation_level=None)
        con.execute("PRAGMA synchronous=FULL")
        con.execute("PRAGMA foreign_keys=ON")
        return con

    def _state(self, con, uid: str, route: str):
        if route not in CAPACITIES:
            raise DomainError("unknown_route", 422)
        now, cap = int(self.clock()), CAPACITIES[route]
        con.execute("INSERT OR IGNORE INTO hearts VALUES(?,?,?,?,1)",
                    (uid, route, cap * HOUR_MS, now))
        credit, anchor, revision = con.execute(
            "SELECT credit_ms,anchor_ms,revision FROM hearts WHERE uid=? AND route=?",
            (uid, route)).fetchone()
        effective = max(now, anchor)  # clock rollback never creates extra recovery
        credit = min(cap * HOUR_MS, credit + effective - anchor)
        return credit, effective, revision, cap

    @staticmethod
    def _reply(route, credit, anchor, revision, cap):
        return {"route": route, "available": credit // HOUR_MS, "maximum": cap,
                "server_time_ms": anchor, "revision": revision,
                "next_heart_at_ms": None if credit == cap * HOUR_MS else
                anchor + HOUR_MS - credit % HOUR_MS}

    def hearts(self, uid: str, route: str, operation: str | None = None):
        con = self.connect()
        try:
            con.execute("BEGIN IMMEDIATE")
            if operation is not None:
                previous = con.execute("SELECT route,reply FROM receipts WHERE uid=? AND operation=?",
                                       (uid, operation)).fetchone()
                if previous:
                    if previous[0] != route:
                        raise DomainError("idempotency_key_reused_for_different_route")
                    con.commit()
                    return {"replayed": True, **json.loads(previous[1])}
            credit, anchor, revision, cap = self._state(con, uid, route)
            if operation is not None:
                if credit < HOUR_MS:
                    # Persist the empty result too: retrying an old denied operation
                    # after recovery must not silently become a new charge.
                    reply = {"status": "empty", **self._reply(route, credit, anchor, revision, cap)}
                else:
                    credit -= HOUR_MS
                    revision += 1
                    reply = {"status": "charged", **self._reply(route, credit, anchor, revision, cap)}
                con.execute("INSERT INTO receipts VALUES(?,?,?,?)",
                            (uid, operation, route, json.dumps(reply, separators=(",", ":"))))
            else:
                reply = {"status": "balance", **self._reply(route, credit, anchor, revision, cap)}
            con.execute("UPDATE hearts SET credit_ms=?,anchor_ms=?,revision=? WHERE uid=? AND route=?",
                        (credit, anchor, revision, uid, route))
            con.commit()
            return {"replayed": False, **reply}
        except Exception:
            con.rollback()
            raise
        finally:
            con.close()

    def complete(self, uid: str, kind: str, course: str, item: str):
        if kind not in {"unit", "course"} or (kind == "course" and item != course):
            raise DomainError("invalid_completion", 422)
        con = self.connect()
        try:
            con.execute("BEGIN IMMEDIATE")
            existing = con.execute(
                "SELECT 1 FROM completions WHERE uid=? AND kind=? AND course=? AND item=?",
                (uid, kind, course, item)).fetchone()
            if not existing and con.execute("SELECT COUNT(*) FROM completions WHERE uid=?", (uid,)).fetchone()[0] >= 20000:
                raise DomainError("completion_limit", 409)
            con.execute("INSERT OR IGNORE INTO completions VALUES(?,?,?,?,?)",
                        (uid, kind, course, item, int(self.clock())))
            con.commit()
            return {"saved": True, "kind": kind, "course": course, "item": item}
        except Exception:
            con.rollback()
            raise
        finally:
            con.close()

    def progress(self, uid: str):
        con = self.connect()
        try:
            rows = con.execute("SELECT kind,course,item,completed_ms FROM completions WHERE uid=? ORDER BY kind,course,item",
                               (uid,)).fetchall()
            return {"completed": [dict(zip(("kind", "course", "item", "completed_ms"), row)) for row in rows]}
        finally:
            con.close()

    def backup(self, destination: Path):
        # SQLite's backup API also copies committed WAL data; do not just copy the .db.
        if destination.exists():
            raise FileExistsError(destination)
        source = self.connect()
        target = sqlite3.connect(destination)
        try:
            source.backup(target)
            if target.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
                raise RuntimeError("Backup integrity failed")
        finally:
            source.close()
            target.close()
        destination.chmod(0o600)


class FirebaseVerifier:
    """Only Google-provider Firebase ID tokens. Never trust an APK-supplied uid."""
    def __init__(self, project: str):
        self.project, self.keys, self.expires, self.next_fetch = project, {}, 0.0, 0.0
        self.lock = threading.Lock()

    def _key(self, kid):
        with self.lock:
            now = time.monotonic()
            if now < self.expires and kid in self.keys:
                return self.keys[kid]
            if now < self.next_fetch:
                raise ValueError("Unknown signing key or key service unavailable")
            self.next_fetch = now + 60  # cap refresh attempts, including random attacker kids
            response = httpx.get(KEYS_URL, timeout=8, follow_redirects=False, trust_env=False)
            response.raise_for_status()
            certs = response.json()
            if not isinstance(certs, dict) or not 1 <= len(certs) <= 20:
                raise ValueError("Invalid key service response")
            keys = {key: x509.load_pem_x509_certificate(pem.encode()).public_key()
                    for key, pem in certs.items()}
            max_age = re.search(r"max-age=(\d+)", response.headers.get("cache-control", ""))
            self.expires = now + min(int(max_age[1]) if max_age else 300, 86400)
            self.keys = keys
            if kid not in keys:
                raise ValueError("Unknown signing key")
            return keys[kid]

    def verify(self, token: str) -> str:
        header = jwt.get_unverified_header(token)
        kid = header.get("kid", "")
        if header.get("alg") != "RS256" or not isinstance(kid, str) or not 1 <= len(kid) <= 128:
            raise ValueError("Invalid JWT header")
        payload = jwt.decode(token, self._key(kid), algorithms=["RS256"], audience=self.project,
                             issuer=f"https://securetoken.google.com/{self.project}", leeway=30,
                             options={"require": ["exp", "iat", "sub", "aud", "iss", "auth_time"]})
        uid, auth_time = payload["sub"], payload["auth_time"]
        if not isinstance(uid, str) or not 1 <= len(uid) <= 128:
            raise ValueError("Invalid subject")
        if not isinstance(auth_time, int) or auth_time > time.time() + 30:
            raise ValueError("Invalid authentication time")
        provider = payload.get("firebase", {})
        if not isinstance(provider, dict) or provider.get("sign_in_provider") != "google.com":
            raise ValueError("Google sign-in is required")
        return uid


class PerPeerLimit:
    """Bounded in-process limiter, not a DDoS protection service."""
    def __init__(self, quota=120):
        self.quota, self.entries, self.lock = quota, OrderedDict(), threading.Lock()

    def allow(self, peer: str) -> bool:
        now = time.monotonic()
        with self.lock:
            started, count = self.entries.pop(peer, (now, 0))
            if now - started >= 60:
                started, count = now, 0
            self.entries[peer] = (started, count + 1)
            if len(self.entries) > 10000:
                self.entries.popitem(last=False)
            return count < self.quota


class SmallBodies:
    def __init__(self, app, maximum=16384):
        self.app, self.maximum = app, maximum

    async def __call__(self, scope, receive, send):
        if scope["type"] != "http":
            await self.app(scope, receive, send)
            return
        chunks, size = [], 0
        try:
            async with asyncio.timeout(10):
                while True:
                    message = await receive()
                    if message["type"] == "http.disconnect":
                        return
                    size += len(message.get("body", b""))
                    if size > self.maximum:
                        await JSONResponse({"error": "request_too_large"}, 413)(scope, receive, send)
                        return
                    chunks.append(message)
                    if not message.get("more_body", False):
                        break
        except TimeoutError:
            await JSONResponse({"error": "request_timeout"}, 408)(scope, receive, send)
            return
        index = 0
        async def replay():
            nonlocal index
            if index < len(chunks):
                message = chunks[index]
                index += 1
                return message
            return await receive()
        await self.app(scope, replay, send)


class Debit(BaseModel):
    model_config = ConfigDict(extra="forbid")
    operation_id: UUID
    route: Literal["programming", "math_physics"]


class Completion(BaseModel):
    model_config = ConfigDict(extra="forbid", str_strip_whitespace=False)
    kind: Literal["unit", "course"]
    course: str = Field(pattern=r"^[a-zA-Z0-9_-]{1,128}$")
    item: str = Field(pattern=r"^[a-zA-Z0-9_-]{1,128}$")


def create_app(settings: Settings, db: Database | None = None, verifier=None) -> FastAPI:
    settings.validate()
    database = db or Database(settings.state / "servertec.db")
    identity = verifier or FirebaseVerifier(settings.project_id)
    limiter = PerPeerLimit()
    app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None, debug=False)
    app.add_middleware(SmallBodies)

    @app.middleware("http")
    async def transport(request, call_next):
        peer = request.client.host if request.client else "unknown"
        if request.url.scheme != "https":
            return JSONResponse({"error": "https_required"}, 400)
        if not limiter.allow(peer):
            return JSONResponse({"error": "rate_limited"}, 429, headers={"Retry-After": "60"})
        response = await call_next(request)
        response.headers["Cache-Control"] = "no-store"
        response.headers["X-Content-Type-Options"] = "nosniff"
        return response

    @app.exception_handler(DomainError)
    async def domain_error(request, error):
        return JSONResponse({"error": error.code}, error.status)

    @app.exception_handler(RequestValidationError)
    async def invalid_body(request, error):
        return JSONResponse({"error": "invalid_request"}, 422)

    @app.exception_handler(sqlite3.OperationalError)
    async def storage_busy(request, error):
        # Never report success after a storage failure. Same operation_id is safe to retry.
        return JSONResponse({"error": "storage_unavailable"}, 503, headers={"Retry-After": "3"})

    async def principal(request: Request):
        header = request.headers.get("Authorization", "")
        if not header.startswith("Bearer ") or len(header) > 8192:
            raise HTTPException(401, "Authentication required")
        token = header[7:]
        if settings.testing:
            try:
                loopback = ipaddress.ip_address(request.client.host).is_loopback
            except (ValueError, AttributeError):
                loopback = False
            if loopback and hmac.compare_digest(hashlib.sha256(token.encode()).hexdigest(), settings.testing_token_hash):
                return "local-test-user"
            raise HTTPException(401, "Invalid testing credential")
        try:
            return await run_in_threadpool(identity.verify, token)
        except httpx.HTTPError:
            raise HTTPException(503, "Identity service temporarily unavailable") from None
        except (ValueError, jwt.PyJWTError, TypeError, KeyError):
            raise HTTPException(401, "Invalid identity token") from None

    @app.get("/health")
    def health():
        return {"status": "ok", "service": "ServerTec"}

    @app.get("/v1/hearts/{route}")
    def hearts(route: str, uid: str = Depends(principal)):
        return database.hearts(uid, route)

    @app.post("/v1/hearts/debits")
    def debit(body: Debit, uid: str = Depends(principal)):
        return database.hearts(uid, body.route, str(body.operation_id))

    @app.get("/v1/progress")
    def progress(uid: str = Depends(principal)):
        return database.progress(uid)

    @app.put("/v1/progress/completions")
    def complete(body: Completion, uid: str = Depends(principal)):
        return database.complete(uid, body.kind, body.course, body.item)

    return app
