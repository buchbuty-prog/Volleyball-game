"""Encryption for OAuth tokens and other secrets stored in the database."""

import json
from functools import lru_cache

from cryptography.fernet import Fernet, InvalidToken, MultiFernet

from .config import get_settings


class TokenDecryptError(Exception):
    pass


class TokenCipher:
    def __init__(self, keys: str):
        fernets = [Fernet(k.strip().encode()) for k in keys.split(",") if k.strip()]
        self._f = MultiFernet(fernets)

    def encrypt(self, plaintext: str) -> str:
        return self._f.encrypt(plaintext.encode()).decode()

    def decrypt(self, ciphertext: str) -> str:
        try:
            return self._f.decrypt(ciphertext.encode()).decode()
        except InvalidToken as e:
            raise TokenDecryptError(
                "Stored token could not be decrypted. Was TOKEN_ENCRYPTION_KEYS changed "
                "without keeping the old key? Reconnect the account."
            ) from e

    def encrypt_json(self, data: dict) -> str:
        return self.encrypt(json.dumps(data))

    def decrypt_json(self, ciphertext: str) -> dict:
        return json.loads(self.decrypt(ciphertext))

    def rotate(self, ciphertext: str) -> str:
        """Re-encrypt with the current primary key."""
        return self._f.rotate(ciphertext.encode()).decode()


@lru_cache
def get_cipher() -> TokenCipher:
    return TokenCipher(get_settings().token_encryption_keys)


def generate_key() -> str:
    return Fernet.generate_key().decode()
