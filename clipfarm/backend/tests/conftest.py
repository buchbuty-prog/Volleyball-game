"""Tests run on SQLite by default. Set TEST_DATABASE_URL to a Postgres database
to run them against Postgres with the real migrations (the database is wiped)."""

import os

from cryptography.fernet import Fernet

os.environ.setdefault("SECRET_KEY", "test-secret-key-" + "x" * 40)
os.environ.setdefault("TOKEN_ENCRYPTION_KEYS", Fernet.generate_key().decode())
os.environ["STORAGE_BACKEND"] = "local"
os.environ.setdefault("DATABASE_URL", "sqlite://")

from datetime import date, timedelta  # noqa: E402

import pytest  # noqa: E402
from fastapi.testclient import TestClient  # noqa: E402
from sqlalchemy import text  # noqa: E402
from sqlalchemy.orm import Session, sessionmaker  # noqa: E402

from clipfarm.auth import hash_password  # noqa: E402
from clipfarm.db import get_db, make_engine  # noqa: E402
from clipfarm.models import Base, User  # noqa: E402
from clipfarm.rights import service as svc  # noqa: E402
from clipfarm.storage import LocalStorage  # noqa: E402

PG_URL = os.environ.get("TEST_DATABASE_URL")
PDF = b"%PDF-1.4\n% signed agreement\n1 0 obj << >> endobj\ntrailer << >>\n%%EOF\n"
TODAY = date(2026, 11, 10)


@pytest.fixture
def engine(tmp_path):
    if PG_URL:
        from alembic import command
        from alembic.config import Config

        eng = make_engine(PG_URL)
        with eng.begin() as conn:
            conn.execute(text("DROP SCHEMA public CASCADE; CREATE SCHEMA public;"))
        cfg = Config(os.path.join(os.path.dirname(__file__), "..", "alembic.ini"))
        cfg.set_main_option("script_location",
                            os.path.join(os.path.dirname(__file__), "..", "migrations"))
        cfg.attributes["url"] = PG_URL
        command.upgrade(cfg, "head")
    else:
        eng = make_engine(f"sqlite:///{tmp_path / 'test.db'}")
        Base.metadata.create_all(eng)
    yield eng
    eng.dispose()


@pytest.fixture
def Sessions(engine):
    return sessionmaker(bind=engine, expire_on_commit=False)


@pytest.fixture
def db(Sessions):
    s: Session = Sessions()
    yield s
    s.rollback()
    s.close()


@pytest.fixture
def storage(tmp_path):
    return LocalStorage(tmp_path / "store")


@pytest.fixture
def make_creator(db):
    def _make(name="Sunny Kids Club", **kw):
        return svc.create_creator(db, actor="tester", name=name, **kw)
    return _make


@pytest.fixture
def make_agreement(db, storage):
    def _make(creator, platforms=("youtube", "instagram"), start=TODAY - timedelta(days=30),
              expiry=TODAY + timedelta(days=335), mode="our_accounts", share="30"):
        terms = svc.AgreementInput(list(platforms), mode, share, start, expiry)
        return svc.create_agreement(db, storage, actor="tester", creator_id=creator.id,
                                    terms=terms, file_bytes=PDF, file_name="agreement.pdf")
    return _make


@pytest.fixture
def app(Sessions, storage):
    from clipfarm.api import routes as api_routes
    from clipfarm.main import create_app
    from clipfarm.web import routes as web_routes

    application = create_app()

    def _db():
        s = Sessions()
        try:
            yield s
            s.commit()
        except Exception:
            s.rollback()
            raise
        finally:
            s.close()

    application.dependency_overrides[get_db] = _db
    application.dependency_overrides[api_routes._storage] = lambda: storage
    application.dependency_overrides[web_routes._storage] = lambda: storage
    return application


@pytest.fixture
def client(app):
    return TestClient(app, base_url="http://testserver")


@pytest.fixture
def admin(Sessions):
    with Sessions() as s:
        s.add(User(username="haim-admin", password_hash=hash_password("correct horse battery")))
        s.commit()
    return ("haim-admin", "correct horse battery")


@pytest.fixture
def logged_in(client, admin):
    r = client.post("/login", data={"username": admin[0], "password": admin[1]},
                    follow_redirects=False)
    assert r.status_code == 303 and r.headers["location"] == "/"
    return client
