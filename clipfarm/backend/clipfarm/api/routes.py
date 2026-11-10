from datetime import date

from fastapi import APIRouter, Depends, File, Form, Query, UploadFile
from sqlalchemy import select
from sqlalchemy.orm import Session

from ..auth import current_user_api
from ..db import get_db
from ..models import AuditLog, Creator, User
from ..rights import gate
from ..rights import service as svc
from ..storage import Storage, get_storage
from . import schemas as s

router = APIRouter(prefix="/api", tags=["api"])


def _storage() -> Storage:
    return get_storage()


@router.get("/creators", response_model=list[s.CreatorOut])
def list_creators(db: Session = Depends(get_db), _: User = Depends(current_user_api)):
    return db.scalars(select(Creator).order_by(Creator.name)).all()


@router.post("/creators", response_model=s.CreatorOut, status_code=201)
def create_creator(body: s.CreatorIn, db: Session = Depends(get_db),
                   user: User = Depends(current_user_api)):
    return svc.create_creator(db, actor=user.username, **body.model_dump())


@router.get("/creators/{creator_id}", response_model=s.CreatorOut)
def get_creator(creator_id: int, db: Session = Depends(get_db), _: User = Depends(current_user_api)):
    return svc._get_creator(db, creator_id)


@router.patch("/creators/{creator_id}", response_model=s.CreatorOut)
def patch_creator(creator_id: int, body: s.CreatorPatch, db: Session = Depends(get_db),
                  user: User = Depends(current_user_api)):
    return svc.update_creator(db, actor=user.username, creator_id=creator_id,
                              **body.model_dump(exclude_unset=True))


@router.post("/creators/{creator_id}/status", response_model=s.CreatorOut)
def set_status(creator_id: int, body: s.StatusIn, db: Session = Depends(get_db),
               user: User = Depends(current_user_api)):
    try:
        return svc.set_creator_status(db, actor=user.username, creator_id=creator_id,
                                      status=body.status, reason=body.reason)
    except ValueError as e:
        raise svc.RegistryError(str(e)) from None


@router.post("/creators/{creator_id}/channels", response_model=s.ChannelOut, status_code=201)
def add_channel(creator_id: int, body: s.ChannelIn, db: Session = Depends(get_db),
                user: User = Depends(current_user_api)):
    return svc.add_channel(db, actor=user.username, creator_id=creator_id, **body.model_dump())


@router.delete("/channels/{channel_id}", status_code=204)
def delete_channel(channel_id: int, db: Session = Depends(get_db),
                   user: User = Depends(current_user_api)):
    svc.remove_channel(db, actor=user.username, channel_id=channel_id)


@router.post("/creators/{creator_id}/agreements", response_model=s.AgreementOut, status_code=201)
async def create_agreement(
    creator_id: int,
    file: UploadFile = File(...),
    allowed_platforms: list[str] = Form(...),
    posting_mode: str = Form(...),
    revenue_share_creator_pct: str = Form(...),
    start_date: str = Form(...),
    expiry_date: str = Form(...),
    notes: str | None = Form(None),
    db: Session = Depends(get_db),
    user: User = Depends(current_user_api),
    storage: Storage = Depends(_storage),
):
    data = await file.read()
    terms = svc.AgreementInput(allowed_platforms, posting_mode, revenue_share_creator_pct,
                               start_date, expiry_date, notes)
    return svc.create_agreement(db, storage, actor=user.username, creator_id=creator_id,
                                terms=terms, file_bytes=data, file_name=file.filename or "")


@router.post("/agreements/{agreement_id}/revoke", response_model=s.AgreementOut)
def revoke(agreement_id: int, body: s.RevokeIn, db: Session = Depends(get_db),
           user: User = Depends(current_user_api)):
    return svc.revoke_agreement(db, actor=user.username, agreement_id=agreement_id,
                                reason=body.reason)


@router.get("/creators/{creator_id}/protected-terms", response_model=list[s.ProtectedTermOut])
def list_terms(creator_id: int, db: Session = Depends(get_db), _: User = Depends(current_user_api)):
    return svc._get_creator(db, creator_id).protected_terms


@router.post("/creators/{creator_id}/protected-terms", response_model=s.ProtectedTermOut,
             status_code=201)
def add_term(creator_id: int, body: s.ProtectedTermIn, db: Session = Depends(get_db),
             user: User = Depends(current_user_api)):
    return svc.add_protected_term(db, actor=user.username, creator_id=creator_id,
                                  term=body.term, kind=body.kind)


@router.delete("/protected-terms/{term_id}", status_code=204)
def delete_term(term_id: int, db: Session = Depends(get_db), user: User = Depends(current_user_api)):
    svc.remove_protected_term(db, actor=user.username, term_id=term_id)


@router.get("/rights/check")
def rights_check(creator_id: int, platform: str, on: date | None = None,
                 db: Session = Depends(get_db), _: User = Depends(current_user_api)):
    """Read-only preview of the gate (not logged; the pipeline uses gate.require)."""
    try:
        return gate.evaluate(db, creator_id, platform, on).as_dict()
    except ValueError:
        raise svc.RegistryError(f"Unknown platform: {platform}") from None


@router.get("/audit", response_model=list[s.AuditOut])
def audit_log(entity_type: str | None = None, entity_id: str | None = None,
              actor: str | None = None, limit: int = Query(200, le=1000),
              db: Session = Depends(get_db), _: User = Depends(current_user_api)):
    q = select(AuditLog).order_by(AuditLog.id.desc()).limit(limit)
    if entity_type:
        q = q.where(AuditLog.entity_type == entity_type)
    if entity_id:
        q = q.where(AuditLog.entity_id == entity_id)
    if actor:
        q = q.where(AuditLog.actor == actor)
    return db.scalars(q).all()
