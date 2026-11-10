"""S3-compatible object storage (MinIO locally, S3/R2 in production).

A local-filesystem backend exists for tests and quick experiments.
"""

from __future__ import annotations

import io
from functools import lru_cache
from pathlib import Path
from typing import BinaryIO, Iterator, Protocol

from .config import get_settings


class StorageError(Exception):
    pass


class Storage(Protocol):
    def put(self, key: str, data: bytes | BinaryIO, content_type: str) -> None: ...
    def open(self, key: str) -> Iterator[bytes]: ...
    def exists(self, key: str) -> bool: ...
    def delete(self, key: str) -> None: ...


def _safe_key(key: str) -> str:
    if not key or key.startswith("/") or ".." in key.split("/"):
        raise StorageError(f"Invalid storage key: {key!r}")
    return key


class LocalStorage:
    def __init__(self, root: str | Path):
        self.root = Path(root).resolve()
        self.root.mkdir(parents=True, exist_ok=True)

    def _path(self, key: str) -> Path:
        return self.root / _safe_key(key)

    def put(self, key, data, content_type):
        p = self._path(key)
        p.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(data, bytes):
            p.write_bytes(data)
        else:
            with p.open("wb") as f:
                while chunk := data.read(1024 * 1024):
                    f.write(chunk)

    def open(self, key):
        p = self._path(key)
        if not p.exists():
            raise StorageError(f"Object not found: {key}")
        with p.open("rb") as f:
            while chunk := f.read(1024 * 1024):
                yield chunk

    def exists(self, key):
        return self._path(key).exists()

    def delete(self, key):
        self._path(key).unlink(missing_ok=True)


class S3Storage:
    def __init__(self, bucket: str, endpoint_url: str | None, access_key: str | None,
                 secret_key: str | None, region: str):
        import boto3
        from botocore.config import Config

        self.bucket = bucket
        self.client = boto3.client(
            "s3",
            endpoint_url=endpoint_url,
            aws_access_key_id=access_key,
            aws_secret_access_key=secret_key,
            region_name=region,
            config=Config(retries={"max_attempts": 5, "mode": "adaptive"},
                          s3={"addressing_style": "path"}),
        )

    def put(self, key, data, content_type):
        from botocore.exceptions import BotoCoreError, ClientError

        body = io.BytesIO(data) if isinstance(data, bytes) else data
        try:
            self.client.upload_fileobj(body, self.bucket, _safe_key(key),
                                       ExtraArgs={"ContentType": content_type})
        except (BotoCoreError, ClientError) as e:
            raise StorageError(f"Could not store {key} in bucket {self.bucket}: {e}") from e

    def open(self, key):
        from botocore.exceptions import BotoCoreError, ClientError

        try:
            obj = self.client.get_object(Bucket=self.bucket, Key=_safe_key(key))
        except (BotoCoreError, ClientError) as e:
            raise StorageError(f"Could not read {key}: {e}") from e
        yield from obj["Body"].iter_chunks(1024 * 1024)

    def exists(self, key):
        from botocore.exceptions import ClientError

        try:
            self.client.head_object(Bucket=self.bucket, Key=_safe_key(key))
            return True
        except ClientError:
            return False

    def delete(self, key):
        self.client.delete_object(Bucket=self.bucket, Key=_safe_key(key))


@lru_cache
def get_storage() -> Storage:
    s = get_settings()
    if s.storage_backend == "local":
        return LocalStorage(s.local_storage_dir)
    return S3Storage(s.s3_bucket, s.s3_endpoint_url, s.s3_access_key, s.s3_secret_key, s.s3_region)
