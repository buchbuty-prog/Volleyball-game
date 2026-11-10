"""All configuration comes from environment variables (or a local .env file).

Secrets have no defaults: the app refuses to start without them rather than
falling back to something insecure.
"""

from functools import lru_cache
from typing import Literal

from pydantic import Field, field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    app_name: str = "ClipFarm"
    # Dates such as agreement start/expiry are evaluated in this timezone.
    timezone: str = "UTC"

    database_url: str = "postgresql+psycopg://clipfarm:clipfarm@localhost:5432/clipfarm"
    redis_url: str = "redis://localhost:6379/0"

    # Required secrets.
    secret_key: str = Field(min_length=32)
    # Comma-separated Fernet keys. The first one encrypts; all of them decrypt,
    # so a key can be rotated by putting the new key first.
    token_encryption_keys: str

    session_cookie_secure: bool = False
    session_max_age_hours: int = 12

    storage_backend: Literal["s3", "local"] = "s3"
    s3_endpoint_url: str | None = None
    s3_access_key: str | None = None
    s3_secret_key: str | None = None
    s3_bucket: str = "clipfarm"
    s3_region: str = "us-east-1"
    local_storage_dir: str = "./data"

    max_agreement_file_mb: int = 20

    @field_validator("token_encryption_keys")
    @classmethod
    def _keys_present(cls, v: str) -> str:
        if not [k for k in v.split(",") if k.strip()]:
            raise ValueError("TOKEN_ENCRYPTION_KEYS must contain at least one Fernet key")
        return v


@lru_cache
def get_settings() -> Settings:
    return Settings()  # type: ignore[call-arg]
