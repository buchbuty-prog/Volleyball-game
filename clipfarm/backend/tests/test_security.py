import pytest
from cryptography.fernet import Fernet

from clipfarm.auth import hash_password, verify_password
from clipfarm.crypto import TokenCipher, TokenDecryptError


def test_token_round_trip_and_ciphertext_hides_token():
    c = TokenCipher(Fernet.generate_key().decode())
    blob = c.encrypt_json({"refresh_token": "1//secret-refresh"})
    assert "secret-refresh" not in blob
    assert c.decrypt_json(blob)["refresh_token"] == "1//secret-refresh"


def test_key_rotation_keeps_old_tokens_readable():
    old, new = Fernet.generate_key().decode(), Fernet.generate_key().decode()
    blob = TokenCipher(old).encrypt("tok")
    rotated = TokenCipher(f"{new},{old}")
    assert rotated.decrypt(blob) == "tok"
    assert TokenCipher(new).decrypt(rotated.rotate(blob)) == "tok"


def test_wrong_key_gives_clear_error():
    blob = TokenCipher(Fernet.generate_key().decode()).encrypt("tok")
    with pytest.raises(TokenDecryptError, match="Reconnect the account"):
        TokenCipher(Fernet.generate_key().decode()).decrypt(blob)


def test_password_hashing():
    h = hash_password("long enough password")
    assert verify_password("long enough password", h)
    assert not verify_password("wrong", h)
    with pytest.raises(ValueError):
        hash_password("short")
