"""The rights gate. Every pipeline step calls this before touching a creator's
content: intake, moment finding, rendering, review approval and (again,
immediately before upload) publishing.

A creator's content is allowed for a platform on a given day only if ALL hold:
  * the creator exists and is not suspended
  * they have an agreement that is not revoked
  * that agreement has a signed file uploaded
  * start_date <= day <= expiry_date
  * the agreement lists that platform
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import date, datetime
from zoneinfo import ZoneInfo

from sqlalchemy import select
from sqlalchemy.orm import Session

from .. import audit
from ..config import get_settings
from ..models import Agreement, AgreementStatus, Creator, CreatorStatus, Platform


@dataclass(frozen=True)
class GateDecision:
    allowed: bool
    creator_id: int
    platform: Platform
    on: date
    agreement_id: int | None = None
    reasons: tuple[str, ...] = ()

    def as_dict(self) -> dict:
        return {"allowed": self.allowed, "creator_id": self.creator_id,
                "platform": self.platform.value, "on": self.on.isoformat(),
                "agreement_id": self.agreement_id, "reasons": list(self.reasons)}


class RightsDenied(Exception):
    def __init__(self, decisions: GateDecision | list[GateDecision]):
        self.decisions = decisions if isinstance(decisions, list) else [decisions]
        reasons = [r for d in self.decisions for r in d.reasons]
        super().__init__("Rights check failed: " + "; ".join(reasons))

    @property
    def reasons(self) -> list[str]:
        return [r for d in self.decisions for r in d.reasons]


def today() -> date:
    return datetime.now(ZoneInfo(get_settings().timezone)).date()


def agreement_problems(a: Agreement, platform: Platform, on: date) -> list[str]:
    label = f"Agreement #{a.id}"
    problems: list[str] = []
    if a.status != AgreementStatus.active:
        when = f" on {a.revoked_at.date().isoformat()}" if a.revoked_at else ""
        problems.append(f"{label} was revoked{when}")
    if not a.file_key:
        problems.append(f"{label} has no signed agreement file uploaded")
    if on < a.start_date:
        problems.append(f"{label} does not start until {a.start_date.isoformat()}")
    if on > a.expiry_date:
        problems.append(f"{label} expired on {a.expiry_date.isoformat()}")
    if not a.covers(platform):
        problems.append(f"{label} does not cover {platform.label}")
    return problems


def agreement_state(a: Agreement, on: date) -> str:
    """Platform-independent label for the UI: active / revoked / expired / not started."""
    if a.status != AgreementStatus.active:
        return "revoked"
    if on > a.expiry_date:
        return "expired"
    if on < a.start_date:
        return "not started"
    return "active"


def evaluate(db: Session, creator_id: int, platform: Platform | str,
             on: date | None = None) -> GateDecision:
    """Pure check, no side effects. Use require() inside the pipeline."""
    platform = Platform(platform)
    on = on or today()

    def deny(*reasons: str) -> GateDecision:
        return GateDecision(False, creator_id, platform, on, None, tuple(reasons))

    creator = db.get(Creator, creator_id)
    if creator is None:
        return deny(f"Creator #{creator_id} does not exist")
    if creator.status != CreatorStatus.active:
        return deny(f"Creator '{creator.name}' is suspended")

    agreements = db.scalars(
        select(Agreement).where(Agreement.creator_id == creator_id)
        .order_by(Agreement.expiry_date.desc(), Agreement.id.desc())
    ).all()
    if not agreements:
        return deny(f"Creator '{creator.name}' has no permission agreement on file")

    reasons: list[str] = []
    for a in agreements:
        problems = agreement_problems(a, platform, on)
        if not problems:
            return GateDecision(True, creator_id, platform, on, a.id, ())
        reasons.extend(problems)
    return deny(*reasons)


def allowed_platforms(db: Session, creator_id: int, on: date | None = None) -> list[Platform]:
    return [p for p in Platform if evaluate(db, creator_id, p, on).allowed]


def require(db: Session, creator_id: int, platform: Platform | str, *, actor: str,
            purpose: str, subject: dict | None = None, on: date | None = None) -> GateDecision:
    """Check and log. Raises RightsDenied if not allowed.

    Allowed checks are logged in the caller's transaction; denials are logged
    in their own transaction so they are kept even when the caller rolls back.
    """
    d = evaluate(db, creator_id, platform, on)
    audit.record(db, actor=actor, action="rights.allowed" if d.allowed else "rights.denied",
                 entity_type="creator", entity_id=creator_id,
                 details={"purpose": purpose, **(subject or {}), **d.as_dict()},
                 independent=not d.allowed)
    if not d.allowed:
        raise RightsDenied(d)
    return d


def require_any(db: Session, creator_id: int, *, actor: str, purpose: str,
                subject: dict | None = None, on: date | None = None) -> list[Platform]:
    """For steps that come before a platform is chosen (e.g. intake): the creator
    must be cleared for at least one platform. Returns the cleared platforms."""
    decisions = [evaluate(db, creator_id, p, on) for p in Platform]
    ok = [d.platform for d in decisions if d.allowed]
    audit.record(db, actor=actor, action="rights.allowed" if ok else "rights.denied",
                 entity_type="creator", entity_id=creator_id,
                 details={"purpose": purpose, **(subject or {}),
                          "allowed_platforms": [p.value for p in ok],
                          "reasons": [r for d in decisions for r in d.reasons]},
                 independent=not ok)
    if not ok:
        raise RightsDenied(decisions)
    return ok
