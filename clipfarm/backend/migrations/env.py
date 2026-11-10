import os
from logging.config import fileConfig

from alembic import context
from sqlalchemy import create_engine

from clipfarm.models import Base

config = context.config
if config.config_file_name:
    fileConfig(config.config_file_name)

target_metadata = Base.metadata


def _url() -> str:
    url = config.attributes.get("url") or os.environ.get("DATABASE_URL")
    if not url:
        raise SystemExit("Set DATABASE_URL to run migrations")
    return url


def run_migrations_offline() -> None:
    context.configure(url=_url(), target_metadata=target_metadata, literal_binds=True,
                      render_as_batch=True)
    with context.begin_transaction():
        context.run_migrations()


def run_migrations_online() -> None:
    connectable = config.attributes.get("connection") or create_engine(_url())
    if hasattr(connectable, "connect"):
        with connectable.connect() as conn:
            _run(conn)
    else:
        _run(connectable)


def _run(conn) -> None:
    context.configure(connection=conn, target_metadata=target_metadata, render_as_batch=True,
                      compare_type=True)
    with context.begin_transaction():
        context.run_migrations()


if context.is_offline_mode():
    run_migrations_offline()
else:
    run_migrations_online()
