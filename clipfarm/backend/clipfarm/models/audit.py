from datetime import datetime

from sqlalchemy import JSON, BigInteger, DateTime, Integer, String
from sqlalchemy.orm import Mapped, mapped_column

from .base import Base, utcnow


class AuditLog(Base):
    """Append-only. On Postgres a trigger rejects UPDATE and DELETE."""

    __tablename__ = "audit_log"

    id: Mapped[int] = mapped_column(BigInteger().with_variant(Integer, "sqlite"), primary_key=True)
    at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, index=True)
    actor: Mapped[str] = mapped_column(String(64), index=True)
    action: Mapped[str] = mapped_column(String(64), index=True)
    entity_type: Mapped[str] = mapped_column(String(32))
    entity_id: Mapped[str | None] = mapped_column(String(64))
    details: Mapped[dict] = mapped_column(JSON, default=dict)
