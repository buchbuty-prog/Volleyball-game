import hashlib
from datetime import timedelta

import pytest
from sqlalchemy import select, text

from clipfarm.models import AuditLog
from clipfarm.rights import service as svc

from .conftest import PDF, PG_URL, TODAY


def terms(**kw):
    base = dict(allowed_platforms=["youtube"], posting_mode="our_accounts",
                revenue_share_creator_pct="40", start_date=TODAY,
                expiry_date=TODAY + timedelta(days=365))
    base.update(kw)
    return svc.AgreementInput(**base)


def add(db, storage, creator, t=None, data=PDF):
    return svc.create_agreement(db, storage, actor="tester", creator_id=creator.id,
                                terms=t or terms(), file_bytes=data, file_name="a.pdf")


def test_agreement_file_is_stored_with_hash(db, storage, make_creator):
    a = add(db, storage, make_creator())
    assert storage.exists(a.file_key)
    assert b"".join(storage.open(a.file_key)) == PDF
    assert a.file_sha256 == hashlib.sha256(PDF).hexdigest()
    assert a.file_content_type == "application/pdf"


@pytest.mark.parametrize("t, msg", [
    (terms(allowed_platforms=[]), "at least one allowed platform"),
    (terms(allowed_platforms=["myspace"]), "Unknown platform"),
    (terms(posting_mode="whatever"), "Posting mode"),
    (terms(revenue_share_creator_pct="120"), "between 0 and 100"),
    (terms(revenue_share_creator_pct="abc"), "must be a number"),
    (terms(expiry_date=TODAY - timedelta(days=1)), "on or after the start date"),
    (terms(start_date="10/11/2026"), "YYYY-MM-DD"),
])
def test_agreement_terms_are_validated(db, storage, make_creator, t, msg):
    with pytest.raises(svc.RegistryError, match=msg):
        add(db, storage, make_creator(), t)


@pytest.mark.parametrize("data, msg", [
    (b"", "Upload the signed"),
    (b"MZ\x90\x00 definitely not a pdf", "PDF, PNG or JPEG"),
    (b"%PDF-" + b"0" * (21 * 1024 * 1024), "larger than"),
])
def test_agreement_file_is_validated(db, storage, make_creator, data, msg):
    with pytest.raises(svc.RegistryError, match=msg):
        add(db, storage, make_creator(), data=data)


def test_revoke_needs_reason_and_only_once(db, storage, make_creator):
    a = add(db, storage, make_creator())
    with pytest.raises(svc.RegistryError, match="reason"):
        svc.revoke_agreement(db, actor="tester", agreement_id=a.id, reason="  ")
    svc.revoke_agreement(db, actor="tester", agreement_id=a.id, reason="expired contract")
    with pytest.raises(svc.RegistryError, match="already revoked"):
        svc.revoke_agreement(db, actor="tester", agreement_id=a.id, reason="again")


def test_every_change_is_audited_with_actor(db, storage, make_creator):
    c = make_creator()
    svc.update_creator(db, actor="tester", creator_id=c.id, name="Sunny Kids")
    svc.add_channel(db, actor="tester", creator_id=c.id, platform="youtube", handle="@sunny")
    a = add(db, storage, c)
    svc.revoke_agreement(db, actor="tester", agreement_id=a.id, reason="test")
    actions = [e.action for e in db.scalars(select(AuditLog).order_by(AuditLog.id))]
    assert actions == ["creator.created", "creator.updated", "channel.added",
                       "agreement.created", "agreement.revoked"]
    assert all(e.actor == "tester" for e in db.scalars(select(AuditLog)))


def test_protected_terms_are_never_written_to_audit_log(db, make_creator):
    c = make_creator()
    svc.add_protected_term(db, actor="tester", creator_id=c.id, term="Emma Johnson", kind="child_name")
    with pytest.raises(svc.RegistryError, match="already protected"):
        svc.add_protected_term(db, actor="tester", creator_id=c.id, term="  emma   JOHNSON ",
                               kind="child_name")
    for e in db.scalars(select(AuditLog)):
        assert "Emma" not in str(e.details) and "Johnson" not in str(e.details)
    assert c.protected_terms[0].masked == "E***"


def test_duplicate_channel_rejected(db, make_creator):
    c = make_creator()
    svc.add_channel(db, actor="tester", creator_id=c.id, platform="tiktok", handle="@sunny")
    with pytest.raises(svc.RegistryError, match="already registered"):
        svc.add_channel(db, actor="tester", creator_id=c.id, platform="tiktok", handle="@sunny")


@pytest.mark.skipif(not PG_URL, reason="append-only trigger is Postgres-only")
def test_audit_log_is_append_only_in_postgres(db, make_creator):
    make_creator()
    db.commit()
    for stmt in ("UPDATE audit_log SET actor = 'someone-else'", "DELETE FROM audit_log"):
        with pytest.raises(Exception, match="append-only"):
            db.execute(text(stmt))
        db.rollback()
