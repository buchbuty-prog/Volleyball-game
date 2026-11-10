"""Server-rendered admin UI. Plain HTML forms; no JavaScript build step."""

from datetime import timedelta
from pathlib import Path
from urllib.parse import quote

from fastapi import APIRouter, Depends, File, Form, Request, UploadFile
from fastapi.responses import RedirectResponse, StreamingResponse
from fastapi.templating import Jinja2Templates
from sqlalchemy import select
from sqlalchemy.orm import Session

from .. import audit
from ..auth import authenticate, current_user_web
from ..db import get_db
from ..models import (Agreement, AgreementStatus, AuditLog, Creator, Platform, PostingMode,
                      TermKind, User)
from ..rights import gate
from ..rights import service as svc
from ..storage import Storage, get_storage

templates = Jinja2Templates(directory=str(Path(__file__).parent / "templates"))
router = APIRouter(include_in_schema=False)


def _storage() -> Storage:
    return get_storage()


def flash(request: Request, message: str, kind: str = "ok") -> None:
    # Reassign (not .append) so the session middleware sees the change and saves it.
    request.session["flash"] = [*request.session.get("flash", []), {"kind": kind, "message": message}]


def render(request: Request, name: str, user: User | None = None, **ctx):
    messages = request.session.pop("flash", [])
    return templates.TemplateResponse(request, name, {
        "user": user, "messages": messages, "Platform": Platform, "PostingMode": PostingMode,
        "TermKind": TermKind, **ctx})


def back(url: str) -> RedirectResponse:
    return RedirectResponse(url, status_code=303)


# ---------- login ----------

@router.get("/login")
def login_page(request: Request):
    return render(request, "login.html")


@router.post("/login")
def login(request: Request, username: str = Form(...), password: str = Form(...),
          db: Session = Depends(get_db)):
    user = authenticate(db, username, password)
    if user is None:
        audit.record(db, actor=username.strip()[:64] or "?", action="auth.login_failed",
                     entity_type="user")
        flash(request, "Wrong username or password", "error")
        return back("/login")
    request.session.clear()
    request.session["user_id"] = user.id
    audit.record(db, actor=user.username, action="auth.login", entity_type="user", entity_id=user.id)
    return back("/")


@router.post("/logout")
def logout(request: Request, db: Session = Depends(get_db), user: User = Depends(current_user_web)):
    audit.record(db, actor=user.username, action="auth.logout", entity_type="user", entity_id=user.id)
    request.session.clear()
    return back("/login")


# ---------- dashboard ----------

@router.get("/")
def dashboard(request: Request, db: Session = Depends(get_db), user: User = Depends(current_user_web)):
    today = gate.today()
    creators = db.scalars(select(Creator).order_by(Creator.name)).all()
    rows = [{"creator": c, "platforms": gate.allowed_platforms(db, c.id, today)} for c in creators]
    expiring = db.scalars(
        select(Agreement).where(Agreement.status == AgreementStatus.active,
                                Agreement.expiry_date >= today,
                                Agreement.expiry_date <= today + timedelta(days=30))
        .order_by(Agreement.expiry_date)).all()
    return render(request, "dashboard.html", user, rows=rows, expiring=expiring, today=today)


# ---------- creators ----------

@router.get("/creators")
def creators_page(request: Request, user: User = Depends(current_user_web)):
    return back("/")


@router.post("/creators")
def create_creator(request: Request, name: str = Form(""), contact_email: str = Form(""),
                   notes: str = Form(""), db: Session = Depends(get_db),
                   user: User = Depends(current_user_web)):
    try:
        c = svc.create_creator(db, actor=user.username, name=name, contact_email=contact_email,
                               notes=notes)
    except svc.RegistryError as e:
        flash(request, str(e), "error")
        return back("/")
    flash(request, f"Creator '{c.name}' added. Next: upload their signed agreement.")
    return back(f"/creators/{c.id}")


@router.get("/creators/{creator_id}")
def creator_page(creator_id: int, request: Request, db: Session = Depends(get_db),
                 user: User = Depends(current_user_web)):
    c = db.get(Creator, creator_id)
    if c is None:
        flash(request, f"Creator #{creator_id} does not exist", "error")
        return back("/")
    today = gate.today()
    decisions = [gate.evaluate(db, c.id, p, today) for p in Platform]
    states = {a.id: gate.agreement_state(a, today) for a in c.agreements}
    log = db.scalars(select(AuditLog).where(AuditLog.entity_type == "creator",
                                            AuditLog.entity_id == str(c.id))
                     .order_by(AuditLog.id.desc()).limit(20)).all()
    return render(request, "creator.html", user, c=c, decisions=decisions, today=today,
                  states=states, log=log)


def _guard(request: Request, creator_id: int, fn):
    try:
        fn()
    except svc.RegistryError as e:
        flash(request, str(e), "error")
    return back(f"/creators/{creator_id}")


@router.post("/creators/{creator_id}/edit")
def edit_creator(creator_id: int, request: Request, name: str = Form(""),
                 contact_email: str = Form(""), notes: str = Form(""),
                 db: Session = Depends(get_db), user: User = Depends(current_user_web)):
    def go():
        svc.update_creator(db, actor=user.username, creator_id=creator_id, name=name,
                           contact_email=contact_email, notes=notes)
        flash(request, "Saved")
    return _guard(request, creator_id, go)


@router.post("/creators/{creator_id}/status")
def creator_status(creator_id: int, request: Request, status: str = Form(...),
                   reason: str = Form(""), db: Session = Depends(get_db),
                   user: User = Depends(current_user_web)):
    def go():
        svc.set_creator_status(db, actor=user.username, creator_id=creator_id, status=status,
                               reason=reason)
        flash(request, f"Creator {'suspended: nothing of theirs can enter the pipeline' if status == 'suspended' else 'reactivated'}")
    return _guard(request, creator_id, go)


@router.post("/creators/{creator_id}/channels")
def add_channel(creator_id: int, request: Request, platform: str = Form(...),
                handle: str = Form(""), url: str = Form(""), db: Session = Depends(get_db),
                user: User = Depends(current_user_web)):
    def go():
        svc.add_channel(db, actor=user.username, creator_id=creator_id, platform=platform,
                        handle=handle, url=url)
        flash(request, "Channel added")
    return _guard(request, creator_id, go)


@router.post("/channels/{channel_id}/delete")
def delete_channel(channel_id: int, request: Request, creator_id: int = Form(...),
                   db: Session = Depends(get_db), user: User = Depends(current_user_web)):
    def go():
        svc.remove_channel(db, actor=user.username, channel_id=channel_id)
        flash(request, "Channel removed")
    return _guard(request, creator_id, go)


@router.post("/creators/{creator_id}/agreements")
async def add_agreement(creator_id: int, request: Request, file: UploadFile = File(None),
                        allowed_platforms: list[str] = Form([]), posting_mode: str = Form(""),
                        revenue_share_creator_pct: str = Form(""), start_date: str = Form(""),
                        expiry_date: str = Form(""), notes: str = Form(""),
                        db: Session = Depends(get_db), user: User = Depends(current_user_web),
                        storage: Storage = Depends(_storage)):
    data = await file.read() if file else b""
    terms = svc.AgreementInput(allowed_platforms, posting_mode, revenue_share_creator_pct,
                               start_date, expiry_date, notes)

    def go():
        a = svc.create_agreement(db, storage, actor=user.username, creator_id=creator_id,
                                 terms=terms, file_bytes=data,
                                 file_name=(file.filename if file else "") or "")
        flash(request, f"Agreement #{a.id} saved")
    return _guard(request, creator_id, go)


@router.post("/agreements/{agreement_id}/revoke")
def revoke_agreement(agreement_id: int, request: Request, creator_id: int = Form(...),
                     reason: str = Form(""), db: Session = Depends(get_db),
                     user: User = Depends(current_user_web)):
    def go():
        svc.revoke_agreement(db, actor=user.username, agreement_id=agreement_id, reason=reason)
        flash(request, f"Agreement #{agreement_id} revoked")
    return _guard(request, creator_id, go)


@router.get("/agreements/{agreement_id}/file")
def agreement_file(agreement_id: int, request: Request, db: Session = Depends(get_db),
                   user: User = Depends(current_user_web), storage: Storage = Depends(_storage)):
    a = db.get(Agreement, agreement_id)
    if a is None:
        flash(request, "Agreement not found", "error")
        return back("/")
    audit.record(db, actor=user.username, action="agreement.file_viewed", entity_type="agreement",
                 entity_id=a.id)
    db.commit()
    return StreamingResponse(storage.open(a.file_key), media_type=a.file_content_type, headers={
        "Content-Disposition": f"inline; filename*=UTF-8''{quote(a.file_name)}",
        "X-Content-Type-Options": "nosniff"})


@router.post("/creators/{creator_id}/protected-terms")
def add_term(creator_id: int, request: Request, term: str = Form(""), kind: str = Form(...),
             db: Session = Depends(get_db), user: User = Depends(current_user_web)):
    def go():
        svc.add_protected_term(db, actor=user.username, creator_id=creator_id, term=term, kind=kind)
        flash(request, "Protected term added")
    return _guard(request, creator_id, go)


@router.post("/protected-terms/{term_id}/delete")
def delete_term(term_id: int, request: Request, creator_id: int = Form(...),
                db: Session = Depends(get_db), user: User = Depends(current_user_web)):
    def go():
        svc.remove_protected_term(db, actor=user.username, term_id=term_id)
        flash(request, "Protected term removed")
    return _guard(request, creator_id, go)


# ---------- audit ----------

@router.get("/audit")
def audit_page(request: Request, actor: str = "", action: str = "", page: int = 1,
               db: Session = Depends(get_db), user: User = Depends(current_user_web)):
    per = 100
    q = select(AuditLog).order_by(AuditLog.id.desc())
    if actor:
        q = q.where(AuditLog.actor == actor)
    if action:
        q = q.where(AuditLog.action.startswith(action))
    entries = db.scalars(q.offset((max(page, 1) - 1) * per).limit(per + 1)).all()
    return render(request, "audit.html", user, entries=entries[:per], has_more=len(entries) > per,
                  page=max(page, 1), actor=actor, action=action)
