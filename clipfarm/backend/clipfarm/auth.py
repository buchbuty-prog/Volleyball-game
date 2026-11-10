"""Admin login. Passwords are hashed with scrypt; sessions are signed cookies."""

import base64
import hashlib
import hmac
import secrets

from fastapi import Depends, Request
from sqlalchemy import select
from sqlalchemy.orm import Session

from .db import get_db
from .models import User

_N, _R, _P = 2**14, 8, 1


def hash_password(password: str) -> str:
    if len(password) < 10:
        raise ValueError("Password must be at least 10 characters")
    salt = secrets.token_bytes(16)
    h = hashlib.scrypt(password.encode(), salt=salt, n=_N, r=_R, p=_P, dklen=32)
    return f"scrypt${_N}${_R}${_P}${base64.b64encode(salt).decode()}${base64.b64encode(h).decode()}"


def verify_password(password: str, stored: str) -> bool:
    try:
        algo, n, r, p, salt_b64, hash_b64 = stored.split("$")
        if algo != "scrypt":
            return False
        h = hashlib.scrypt(password.encode(), salt=base64.b64decode(salt_b64),
                           n=int(n), r=int(r), p=int(p), dklen=32)
        return hmac.compare_digest(h, base64.b64decode(hash_b64))
    except (ValueError, TypeError):
        return False


def authenticate(db: Session, username: str, password: str) -> User | None:
    user = db.scalar(select(User).where(User.username == username.strip()))
    if user is None or not user.is_active:
        # Same cost as a real check, so usernames can't be probed by timing.
        verify_password(password, "scrypt$16384$8$1$AAAAAAAAAAAAAAAAAAAAAA==$" + "A" * 44)
        return None
    return user if verify_password(password, user.password_hash) else None


class LoginRequired(Exception):
    """Raised for web pages; the app turns it into a redirect to /login."""


class ApiAuthRequired(Exception):
    """Raised for /api routes; the app turns it into a 401."""


def _session_user(request: Request, db: Session) -> User | None:
    uid = request.session.get("user_id")
    if uid is None:
        return None
    user = db.get(User, uid)
    return user if user and user.is_active else None


def current_user_web(request: Request, db: Session = Depends(get_db)) -> User:
    user = _session_user(request, db)
    if user is None:
        raise LoginRequired()
    return user


def current_user_api(request: Request, db: Session = Depends(get_db)) -> User:
    user = _session_user(request, db)
    if user is None:
        raise ApiAuthRequired()
    return user
