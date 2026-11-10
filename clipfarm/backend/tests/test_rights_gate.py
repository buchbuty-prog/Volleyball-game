"""The rights gate: no video enters the pipeline unless its creator has an
active, unexpired, signed agreement that covers the platform."""

from datetime import timedelta

import pytest
from sqlalchemy import select

from clipfarm.models import Agreement, AuditLog, Platform, PostingMode
from clipfarm.rights import gate
from clipfarm.rights import service as svc

from .conftest import TODAY


def test_unknown_creator_is_denied(db):
    d = gate.evaluate(db, 999, "youtube", TODAY)
    assert not d.allowed
    assert "does not exist" in d.reasons[0]


def test_creator_without_agreement_is_denied(db, make_creator):
    c = make_creator()
    d = gate.evaluate(db, c.id, Platform.youtube, TODAY)
    assert not d.allowed
    assert "no permission agreement" in d.reasons[0]


def test_active_agreement_allows_covered_platform(db, make_creator, make_agreement):
    c = make_creator()
    a = make_agreement(c, platforms=["youtube"])
    d = gate.evaluate(db, c.id, "youtube", TODAY)
    assert d.allowed and d.agreement_id == a.id and d.reasons == ()


def test_platform_not_in_agreement_is_denied(db, make_creator, make_agreement):
    c = make_creator()
    make_agreement(c, platforms=["youtube", "instagram"])
    d = gate.evaluate(db, c.id, "tiktok", TODAY)
    assert not d.allowed
    assert any("does not cover TikTok" in r for r in d.reasons)


def test_expired_agreement_is_denied_and_expiry_day_is_inclusive(db, make_creator, make_agreement):
    c = make_creator()
    make_agreement(c, start=TODAY - timedelta(days=100), expiry=TODAY)
    assert gate.evaluate(db, c.id, "youtube", TODAY).allowed
    d = gate.evaluate(db, c.id, "youtube", TODAY + timedelta(days=1))
    assert not d.allowed
    assert any(f"expired on {TODAY.isoformat()}" in r for r in d.reasons)


def test_agreement_not_started_is_denied_and_start_day_is_inclusive(db, make_creator, make_agreement):
    c = make_creator()
    make_agreement(c, start=TODAY + timedelta(days=5), expiry=TODAY + timedelta(days=50))
    assert not gate.evaluate(db, c.id, "youtube", TODAY).allowed
    assert gate.evaluate(db, c.id, "youtube", TODAY + timedelta(days=5)).allowed


def test_revoked_agreement_is_denied(db, make_creator, make_agreement):
    c = make_creator()
    a = make_agreement(c)
    svc.revoke_agreement(db, actor="tester", agreement_id=a.id, reason="Creator withdrew consent")
    d = gate.evaluate(db, c.id, "youtube", TODAY)
    assert not d.allowed
    assert any("revoked" in r for r in d.reasons)


def test_suspended_creator_is_denied_even_with_valid_agreement(db, make_creator, make_agreement):
    c = make_creator()
    make_agreement(c)
    svc.set_creator_status(db, actor="tester", creator_id=c.id, status="suspended")
    d = gate.evaluate(db, c.id, "youtube", TODAY)
    assert not d.allowed and "suspended" in d.reasons[0]


def test_agreement_without_signed_file_is_denied(db, make_creator):
    c = make_creator()
    db.add(Agreement(creator_id=c.id, allowed_platforms=["youtube"], posting_mode=PostingMode.our_accounts,
                     revenue_share_creator_pct=50, start_date=TODAY, expiry_date=TODAY,
                     file_key="", file_name="", file_content_type="", file_size=0,
                     file_sha256="", created_by="tester"))
    db.flush()
    d = gate.evaluate(db, c.id, "youtube", TODAY)
    assert not d.allowed and any("no signed agreement file" in r for r in d.reasons)


def test_any_valid_agreement_is_enough(db, make_creator, make_agreement):
    c = make_creator()
    make_agreement(c, start=TODAY - timedelta(days=400), expiry=TODAY - timedelta(days=35))
    current = make_agreement(c, platforms=["tiktok"])
    assert gate.evaluate(db, c.id, "tiktok", TODAY).agreement_id == current.id
    # The expired one covered YouTube; the current one doesn't.
    assert not gate.evaluate(db, c.id, "youtube", TODAY).allowed


def test_allowed_platforms(db, make_creator, make_agreement):
    c = make_creator()
    make_agreement(c, platforms=["youtube", "tiktok"])
    assert gate.allowed_platforms(db, c.id, TODAY) == [Platform.youtube, Platform.tiktok]


def test_unknown_platform_raises(db, make_creator):
    with pytest.raises(ValueError):
        gate.evaluate(db, make_creator().id, "myspace", TODAY)


def test_require_logs_allowed_check_in_callers_transaction(db, make_creator, make_agreement):
    c = make_creator()
    make_agreement(c)
    d = gate.require(db, c.id, "youtube", actor="worker", purpose="intake",
                     subject={"source_video_id": 7}, on=TODAY)
    assert d.allowed
    entry = db.scalars(select(AuditLog).where(AuditLog.action == "rights.allowed")).one()
    assert entry.actor == "worker"
    assert entry.details["purpose"] == "intake" and entry.details["source_video_id"] == 7


def test_require_raises_and_denial_log_survives_rollback(db, Sessions, make_creator):
    c = make_creator()
    db.commit()
    with pytest.raises(gate.RightsDenied) as exc:
        gate.require(db, c.id, "tiktok", actor="worker", purpose="publish", on=TODAY)
    assert "no permission agreement" in str(exc.value)
    db.rollback()
    with Sessions() as fresh:
        entry = fresh.scalars(select(AuditLog).where(AuditLog.action == "rights.denied")).one()
        assert entry.details["platform"] == "tiktok"
        assert entry.details["purpose"] == "publish"
        assert entry.details["reasons"]


def test_require_any_for_intake(db, Sessions, make_creator, make_agreement):
    c = make_creator()
    make_agreement(c, platforms=["instagram"])
    assert gate.require_any(db, c.id, actor="worker", purpose="intake", on=TODAY) == [Platform.instagram]

    other = make_creator("No Paperwork Yet")
    db.commit()
    with pytest.raises(gate.RightsDenied) as exc:
        gate.require_any(db, other.id, actor="worker", purpose="intake", on=TODAY)
    assert len(exc.value.reasons) == 3  # one per platform
