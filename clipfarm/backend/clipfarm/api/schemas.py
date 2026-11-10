from datetime import date, datetime
from decimal import Decimal

from pydantic import BaseModel, ConfigDict


class CreatorIn(BaseModel):
    name: str
    contact_email: str | None = None
    notes: str | None = None


class CreatorPatch(BaseModel):
    name: str | None = None
    contact_email: str | None = None
    notes: str | None = None


class StatusIn(BaseModel):
    status: str
    reason: str | None = None


class ChannelIn(BaseModel):
    platform: str
    handle: str
    url: str | None = None


class ChannelOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)
    id: int
    platform: str
    handle: str
    url: str | None
    oauth_connected_at: datetime | None


class AgreementOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)
    id: int
    creator_id: int
    allowed_platforms: list[str]
    posting_mode: str
    revenue_share_creator_pct: Decimal
    start_date: date
    expiry_date: date
    status: str
    notes: str | None
    file_name: str
    file_sha256: str
    created_by: str
    created_at: datetime
    revoked_at: datetime | None
    revoked_reason: str | None


class CreatorOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)
    id: int
    name: str
    contact_email: str | None
    notes: str | None
    status: str
    created_at: datetime
    channels: list[ChannelOut] = []
    agreements: list[AgreementOut] = []


class RevokeIn(BaseModel):
    reason: str


class ProtectedTermIn(BaseModel):
    term: str
    kind: str


class ProtectedTermOut(BaseModel):
    """The term itself is never returned by the API, only a masked version."""

    model_config = ConfigDict(from_attributes=True)
    id: int
    kind: str
    masked: str


class AuditOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)
    id: int
    at: datetime
    actor: str
    action: str
    entity_type: str
    entity_id: str | None
    details: dict
