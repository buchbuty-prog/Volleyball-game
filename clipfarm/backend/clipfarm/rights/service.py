"""Creating and changing registry records. Every change is audited.
The web UI and the JSON API both go through these functions."""

from __future__ import annotations

import hashlib
import re
import uuid
from dataclasses import dataclass
from datetime import date
from decimal import Decimal, InvalidOperation

from sqlalchemy import select
from sqlalchemy.orm import Session

from .. import audit
from ..config import get_settings
from ..models import (Agreement, AgreementStatus, Channel, Creator, CreatorStatus, Platform,
                      PostingMode, ProtectedTerm, TermKind, mask_term, utcnow)
from ..storage import Storage


class RegistryError(ValueError):
    """A user-facing validation error."""


EMAIL_RE = re.compile(r"^[^@\s]+@[^@\s]+\.[^@\s]+$")

# Magic bytes, so a renamed .exe is not accepted as a "PDF".
FILE_SIGNATURES = [
    (b"%PDF-", "application/pdf", ".pdf"),
    (b"\x89PNG\r\n\x1a\n", "image/png", ".png"),
    (b"\xff\xd8\xff", "image/jpeg", ".jpg"),
]


@dataclass
class AgreementInput:
    allowed_platforms: list[str]
    posting_mode: str
    revenue_share_creator_pct: str | float | Decimal
    start_date: date | str
    expiry_date: date | str
    notes: str | None = None


def _clean(s: str | None) -> str | None:
    s = (s or "").strip()
    return s or None


def _get_creator(db: Session, creator_id: int) -> Creator:
    c = db.get(Creator, creator_id)
    if c is None:
        raise RegistryError(f"Creator #{creator_id} does not exist")
    return c


# ---------- creators ----------

def create_creator(db: Session, *, actor: str, name: str, contact_email: str | None = None,
                   notes: str | None = None) -> Creator:
    name = _clean(name)
    if not name:
        raise RegistryError("Creator name is required")
    email = _clean(contact_email)
    if email and not EMAIL_RE.match(email):
        raise RegistryError("Contact email is not a valid address")
    c = Creator(name=name, contact_email=email, notes=_clean(notes))
    db.add(c)
    db.flush()
    audit.record(db, actor=actor, action="creator.created", entity_type="creator", entity_id=c.id,
                 details={"name": c.name})
    return c


def update_creator(db: Session, *, actor: str, creator_id: int, name: str | None = None,
                   contact_email: str | None = None, notes: str | None = None) -> Creator:
    c = _get_creator(db, creator_id)
    before = {"name": c.name, "contact_email": c.contact_email, "notes": c.notes}
    if name is not None:
        if not _clean(name):
            raise RegistryError("Creator name is required")
        c.name = _clean(name)
    if contact_email is not None:
        email = _clean(contact_email)
        if email and not EMAIL_RE.match(email):
            raise RegistryError("Contact email is not a valid address")
        c.contact_email = email
    if notes is not None:
        c.notes = _clean(notes)
    after = {"name": c.name, "contact_email": c.contact_email, "notes": c.notes}
    changed = {k: {"from": before[k], "to": after[k]} for k in before if before[k] != after[k]}
    if changed:
        audit.record(db, actor=actor, action="creator.updated", entity_type="creator",
                     entity_id=c.id, details={"changes": changed})
    return c


def set_creator_status(db: Session, *, actor: str, creator_id: int, status: str,
                       reason: str | None = None) -> Creator:
    c = _get_creator(db, creator_id)
    new = CreatorStatus(status)
    if c.status != new:
        c.status = new
        audit.record(db, actor=actor, action=f"creator.{'suspended' if new == CreatorStatus.suspended else 'reactivated'}",
                     entity_type="creator", entity_id=c.id, details={"reason": _clean(reason)})
    return c


# ---------- channels ----------

def add_channel(db: Session, *, actor: str, creator_id: int, platform: str, handle: str,
                url: str | None = None) -> Channel:
    c = _get_creator(db, creator_id)
    try:
        p = Platform(platform)
    except ValueError:
        raise RegistryError(f"Unknown platform: {platform}") from None
    handle = _clean(handle)
    if not handle:
        raise RegistryError("Channel handle is required")
    if db.scalar(select(Channel).where(Channel.platform == p, Channel.handle == handle)):
        raise RegistryError(f"{p.label} channel '{handle}' is already registered")
    ch = Channel(creator_id=c.id, platform=p, handle=handle, url=_clean(url))
    db.add(ch)
    db.flush()
    audit.record(db, actor=actor, action="channel.added", entity_type="channel", entity_id=ch.id,
                 details={"creator_id": c.id, "platform": p.value, "handle": handle})
    return ch


def remove_channel(db: Session, *, actor: str, channel_id: int) -> None:
    ch = db.get(Channel, channel_id)
    if ch is None:
        raise RegistryError(f"Channel #{channel_id} does not exist")
    audit.record(db, actor=actor, action="channel.removed", entity_type="channel", entity_id=ch.id,
                 details={"creator_id": ch.creator_id, "platform": ch.platform.value,
                          "handle": ch.handle})
    db.delete(ch)


# ---------- agreements ----------

def _parse_date(v: date | str, field: str) -> date:
    if isinstance(v, date):
        return v
    try:
        return date.fromisoformat(str(v).strip())
    except ValueError:
        raise RegistryError(f"{field} must be a date (YYYY-MM-DD)") from None


def _validate_terms(t: AgreementInput) -> tuple[list[str], PostingMode, Decimal, date, date]:
    platforms = []
    for p in t.allowed_platforms:
        try:
            platforms.append(Platform(p).value)
        except ValueError:
            raise RegistryError(f"Unknown platform: {p}") from None
    platforms = sorted(set(platforms))
    if not platforms:
        raise RegistryError("Select at least one allowed platform")
    try:
        mode = PostingMode(t.posting_mode)
    except ValueError:
        raise RegistryError("Posting mode must be 'our_accounts' or 'creator_accounts'") from None
    try:
        share = Decimal(str(t.revenue_share_creator_pct).strip())
    except (InvalidOperation, ValueError):
        raise RegistryError("Revenue share must be a number between 0 and 100") from None
    if not (Decimal(0) <= share <= Decimal(100)):
        raise RegistryError("Revenue share must be between 0 and 100")
    start = _parse_date(t.start_date, "Start date")
    expiry = _parse_date(t.expiry_date, "Expiry date")
    if expiry < start:
        raise RegistryError("Expiry date must be on or after the start date")
    return platforms, mode, share.quantize(Decimal("0.01")), start, expiry


def sniff_agreement_file(data: bytes) -> tuple[str, str]:
    for sig, content_type, ext in FILE_SIGNATURES:
        if data.startswith(sig):
            return content_type, ext
    raise RegistryError("The agreement must be a PDF, PNG or JPEG scan")


def create_agreement(db: Session, storage: Storage, *, actor: str, creator_id: int,
                     terms: AgreementInput, file_bytes: bytes, file_name: str) -> Agreement:
    c = _get_creator(db, creator_id)
    platforms, mode, share, start, expiry = _validate_terms(terms)
    if not file_bytes:
        raise RegistryError("Upload the signed permission agreement (PDF or scan)")
    limit = get_settings().max_agreement_file_mb * 1024 * 1024
    if len(file_bytes) > limit:
        raise RegistryError(f"Agreement file is larger than {get_settings().max_agreement_file_mb} MB")
    content_type, ext = sniff_agreement_file(file_bytes)

    key = f"agreements/creator-{c.id}/{uuid.uuid4().hex}{ext}"
    storage.put(key, file_bytes, content_type)
    a = Agreement(
        creator_id=c.id, allowed_platforms=platforms, posting_mode=mode,
        revenue_share_creator_pct=share, start_date=start, expiry_date=expiry,
        notes=_clean(terms.notes), file_key=key,
        file_name=(_clean(file_name) or f"agreement{ext}")[:255],
        file_content_type=content_type, file_size=len(file_bytes),
        file_sha256=hashlib.sha256(file_bytes).hexdigest(), created_by=actor,
    )
    db.add(a)
    db.flush()
    audit.record(db, actor=actor, action="agreement.created", entity_type="agreement",
                 entity_id=a.id, details={
                     "creator_id": c.id, "allowed_platforms": platforms,
                     "posting_mode": mode.value, "revenue_share_creator_pct": str(share),
                     "start_date": start, "expiry_date": expiry,
                     "file_sha256": a.file_sha256, "file_name": a.file_name})
    return a


def revoke_agreement(db: Session, *, actor: str, agreement_id: int, reason: str) -> Agreement:
    a = db.get(Agreement, agreement_id)
    if a is None:
        raise RegistryError(f"Agreement #{agreement_id} does not exist")
    reason = _clean(reason)
    if not reason:
        raise RegistryError("Give a reason for revoking the agreement")
    if a.status == AgreementStatus.revoked:
        raise RegistryError(f"Agreement #{a.id} is already revoked")
    a.status = AgreementStatus.revoked
    a.revoked_at = utcnow()
    a.revoked_by = actor
    a.revoked_reason = reason
    audit.record(db, actor=actor, action="agreement.revoked", entity_type="agreement",
                 entity_id=a.id, details={"creator_id": a.creator_id, "reason": reason})
    return a


# ---------- protected terms ----------

def normalize_term(term: str) -> str:
    return " ".join(term.casefold().split())


def add_protected_term(db: Session, *, actor: str, creator_id: int, term: str,
                       kind: str) -> ProtectedTerm:
    c = _get_creator(db, creator_id)
    term = _clean(term)
    if not term:
        raise RegistryError("Enter the name or place to protect")
    try:
        k = TermKind(kind)
    except ValueError:
        raise RegistryError(f"Unknown term type: {kind}") from None
    norm = normalize_term(term)
    if db.scalar(select(ProtectedTerm).where(ProtectedTerm.creator_id == c.id,
                                             ProtectedTerm.term_normalized == norm)):
        raise RegistryError("That term is already protected for this creator")
    pt = ProtectedTerm(creator_id=c.id, term=term, term_normalized=norm, kind=k)
    db.add(pt)
    db.flush()
    # The audit log never stores the protected term itself.
    audit.record(db, actor=actor, action="protected_term.added", entity_type="protected_term",
                 entity_id=pt.id, details={"creator_id": c.id, "kind": k.value,
                                           "term_masked": mask_term(term)})
    return pt


def remove_protected_term(db: Session, *, actor: str, term_id: int) -> None:
    pt = db.get(ProtectedTerm, term_id)
    if pt is None:
        raise RegistryError(f"Protected term #{term_id} does not exist")
    audit.record(db, actor=actor, action="protected_term.removed", entity_type="protected_term",
                 entity_id=pt.id, details={"creator_id": pt.creator_id, "kind": pt.kind.value,
                                           "term_masked": pt.masked})
    db.delete(pt)
