from enum import Enum


class Platform(str, Enum):
    youtube = "youtube"
    instagram = "instagram"
    tiktok = "tiktok"

    @property
    def label(self) -> str:
        return {"youtube": "YouTube Shorts", "instagram": "Instagram Reels", "tiktok": "TikTok"}[self.value]


class PostingMode(str, Enum):
    """Whose account the clips are published to."""

    our_accounts = "our_accounts"
    creator_accounts = "creator_accounts"


class CreatorStatus(str, Enum):
    active = "active"
    suspended = "suspended"


class AgreementStatus(str, Enum):
    active = "active"
    revoked = "revoked"


class TermKind(str, Enum):
    """Words that must never appear in published text (child safety)."""

    child_name = "child_name"
    school = "school"
    city = "city"
    location = "location"
    other = "other"
