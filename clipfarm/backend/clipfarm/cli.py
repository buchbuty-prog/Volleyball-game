"""Admin commands.

  python -m clipfarm.cli gen-keys              print fresh SECRET_KEY and TOKEN_ENCRYPTION_KEYS
  python -m clipfarm.cli create-user NAME      add an admin (prompts for password)
"""

import argparse
import getpass
import os
import secrets
import sys

from cryptography.fernet import Fernet


def gen_keys() -> None:
    print(f"SECRET_KEY={secrets.token_urlsafe(48)}")
    print(f"TOKEN_ENCRYPTION_KEYS={Fernet.generate_key().decode()}")


def create_user(username: str, password_env: str | None) -> None:
    from sqlalchemy import select

    from . import audit
    from .auth import hash_password
    from .db import get_sessionmaker
    from .models import User

    if password_env:
        password = os.environ.get(password_env, "")
    else:
        password = getpass.getpass("Password (10+ characters): ")
        if password != getpass.getpass("Again: "):
            sys.exit("Passwords don't match")
    try:
        pw_hash = hash_password(password)
    except ValueError as e:
        sys.exit(str(e))
    with get_sessionmaker()() as db:
        if db.scalar(select(User).where(User.username == username)):
            sys.exit(f"User {username!r} already exists")
        u = User(username=username, password_hash=pw_hash)
        db.add(u)
        db.flush()
        audit.record(db, actor="cli", action="user.created", entity_type="user", entity_id=u.id,
                     details={"username": username})
        db.commit()
    print(f"Created user {username!r}")


def main(argv: list[str] | None = None) -> None:
    p = argparse.ArgumentParser(prog="clipfarm")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("gen-keys")
    cu = sub.add_parser("create-user")
    cu.add_argument("username")
    cu.add_argument("--password-env", help="read the password from this environment variable")
    args = p.parse_args(argv)
    if args.cmd == "gen-keys":
        gen_keys()
    elif args.cmd == "create-user":
        create_user(args.username, args.password_env)


if __name__ == "__main__":
    main()
