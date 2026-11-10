from datetime import date, datetime
from decimal import Decimal

from sqlalchemy import (JSON, CheckConstraint, Date, DateTime, Enum, ForeignKey, Integer,
                        Numeric, String, Text, UniqueConstraint)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from .base import Base, utcnow
from .enums import AgreementStatus, CreatorStatus, Platform, PostingMode, TermKind


def _enum(e):
    return Enum(e, native_enum=False, length=32, validate_strings=True)


class User(Base):
    __tablename__ = "users"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    username: Mapped[str] = mapped_column(String(64), unique=True)
    password_hash: Mapped[str] = mapped_column(String(255))
    is_active: Mapped[bool] = mapped_column(default=True)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow)


class Creator(Base):
    __tablename__ = "creators"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    name: Mapped[str] = mapped_column(String(200))
    contact_email: Mapped[str | None] = mapped_column(String(320))
    notes: Mapped[str | None] = mapped_column(Text)
    status: Mapped[CreatorStatus] = mapped_column(_enum(CreatorStatus), default=CreatorStatus.active)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow)
    updated_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, onupdate=utcnow)

    channels: Mapped[list["Channel"]] = relationship(back_populates="creator", cascade="all, delete-orphan",
                                                     order_by="Channel.id")
    agreements: Mapped[list["Agreement"]] = relationship(back_populates="creator", order_by="Agreement.id.desc()")
    protected_terms: Mapped[list["ProtectedTerm"]] = relationship(back_populates="creator",
                                                                  cascade="all, delete-orphan",
                                                                  order_by="ProtectedTerm.id")


class Channel(Base):
    __tablename__ = "channels"
    __table_args__ = (UniqueConstraint("platform", "handle", name="platform_handle"),)

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    creator_id: Mapped[int] = mapped_column(ForeignKey("creators.id", ondelete="CASCADE"), index=True)
    platform: Mapped[Platform] = mapped_column(_enum(Platform))
    handle: Mapped[str] = mapped_column(String(200))
    url: Mapped[str | None] = mapped_column(String(500))
    external_id: Mapped[str | None] = mapped_column(String(200))
    # OAuth token JSON, encrypted with TokenCipher. Never stored in plain text.
    oauth_token_encrypted: Mapped[str | None] = mapped_column(Text)
    oauth_connected_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow)

    creator: Mapped[Creator] = relationship(back_populates="channels")


class Agreement(Base):
    """A signed permission agreement. Terms are immutable once created: to change
    terms, add a new agreement and revoke the old one, so the history is kept."""

    __tablename__ = "agreements"
    __table_args__ = (
        CheckConstraint("expiry_date >= start_date", name="dates_ordered"),
        CheckConstraint("revenue_share_creator_pct >= 0 AND revenue_share_creator_pct <= 100",
                        name="revenue_share_range"),
    )

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    creator_id: Mapped[int] = mapped_column(ForeignKey("creators.id", ondelete="RESTRICT"), index=True)
    allowed_platforms: Mapped[list[str]] = mapped_column(JSON)
    posting_mode: Mapped[PostingMode] = mapped_column(_enum(PostingMode))
    revenue_share_creator_pct: Mapped[Decimal] = mapped_column(Numeric(5, 2))
    start_date: Mapped[date] = mapped_column(Date)
    expiry_date: Mapped[date] = mapped_column(Date)
    status: Mapped[AgreementStatus] = mapped_column(_enum(AgreementStatus), default=AgreementStatus.active)
    notes: Mapped[str | None] = mapped_column(Text)

    file_key: Mapped[str] = mapped_column(String(500))
    file_name: Mapped[str] = mapped_column(String(255))
    file_content_type: Mapped[str] = mapped_column(String(100))
    file_size: Mapped[int] = mapped_column(Integer)
    file_sha256: Mapped[str] = mapped_column(String(64))

    created_by: Mapped[str] = mapped_column(String(64))
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow)
    revoked_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    revoked_by: Mapped[str | None] = mapped_column(String(64))
    revoked_reason: Mapped[str | None] = mapped_column(Text)

    creator: Mapped[Creator] = relationship(back_populates="agreements")

    def covers(self, platform: Platform) -> bool:
        return Platform(platform).value in (self.allowed_platforms or [])


class ProtectedTerm(Base):
    """Children's names, schools, cities etc. that must never be published."""

    __tablename__ = "protected_terms"
    __table_args__ = (UniqueConstraint("creator_id", "term_normalized", name="creator_term"),)

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    creator_id: Mapped[int] = mapped_column(ForeignKey("creators.id", ondelete="CASCADE"), index=True)
    term: Mapped[str] = mapped_column(String(200))
    term_normalized: Mapped[str] = mapped_column(String(200))
    kind: Mapped[TermKind] = mapped_column(_enum(TermKind))
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow)

    creator: Mapped[Creator] = relationship(back_populates="protected_terms")

    @property
    def masked(self) -> str:
        return mask_term(self.term)


def mask_term(term: str) -> str:
    """Audit logs show 'E***' instead of a child's name."""
    term = term.strip()
    return (term[:1] + "***") if term else "***"
