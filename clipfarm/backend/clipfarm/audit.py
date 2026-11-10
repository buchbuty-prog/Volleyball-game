"""Every action is written to the append-only audit log: who, what, when."""

import json
from typing import Any

from sqlalchemy.orm import Session

from .models import AuditLog


def _jsonable(details: dict[str, Any] | None) -> dict:
    return json.loads(json.dumps(details or {}, default=str))


def record(db: Session, *, actor: str, action: str, entity_type: str,
           entity_id: Any = None, details: dict | None = None,
           independent: bool = False) -> None:
    """Add an audit entry to the caller's transaction.

    independent=True writes and commits it in a separate transaction, so the
    entry survives even if the caller's transaction is rolled back (used for
    denials, which are usually followed by an error).
    """
    entry = AuditLog(actor=actor, action=action, entity_type=entity_type,
                     entity_id=None if entity_id is None else str(entity_id),
                     details=_jsonable(details))
    if not independent:
        db.add(entry)
        return
    with Session(bind=db.get_bind()) as side:
        side.add(entry)
        side.commit()
