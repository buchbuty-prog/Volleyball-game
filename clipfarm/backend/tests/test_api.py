from datetime import timedelta

from .conftest import PDF, TODAY


def _agreement(client, creator_id, platforms=("youtube",), start=None, expiry=None):
    start = start or TODAY - timedelta(days=1)
    expiry = expiry or TODAY + timedelta(days=3650)
    return client.post(
        f"/api/creators/{creator_id}/agreements",
        files={"file": ("signed.pdf", PDF, "application/pdf")},
        data={"allowed_platforms": list(platforms), "posting_mode": "creator_accounts",
              "revenue_share_creator_pct": "50", "start_date": start.isoformat(),
              "expiry_date": expiry.isoformat()})


def test_api_requires_login(client):
    assert client.get("/api/creators").status_code == 401
    r = client.get("/", follow_redirects=False)
    assert r.status_code == 303 and r.headers["location"] == "/login"


def test_wrong_password_is_rejected_and_logged(client, admin):
    r = client.post("/login", data={"username": admin[0], "password": "nope"}, follow_redirects=False)
    assert r.headers["location"] == "/login"
    assert client.get("/api/creators").status_code == 401


def test_full_rights_flow(logged_in):
    c = logged_in.post("/api/creators", json={"name": "Little Builders"}).json()
    r = _agreement(logged_in, c["id"], platforms=["youtube", "instagram"])
    assert r.status_code == 201, r.text
    agreement = r.json()

    ok = logged_in.get("/api/rights/check", params={"creator_id": c["id"], "platform": "youtube"}).json()
    assert ok["allowed"] and ok["agreement_id"] == agreement["id"]
    no = logged_in.get("/api/rights/check", params={"creator_id": c["id"], "platform": "tiktok"}).json()
    assert not no["allowed"]

    r = logged_in.post(f"/api/agreements/{agreement['id']}/revoke", json={"reason": "withdrawn"})
    assert r.json()["status"] == "revoked"
    after = logged_in.get("/api/rights/check", params={"creator_id": c["id"], "platform": "youtube"}).json()
    assert not after["allowed"]

    log = logged_in.get("/api/audit").json()
    assert {"creator.created", "agreement.created", "agreement.revoked", "auth.login"} <= {e["action"] for e in log}
    assert all(e["actor"] == "haim-admin" for e in log)


def test_validation_errors_are_clear(logged_in):
    c = logged_in.post("/api/creators", json={"name": "X"}).json()
    r = _agreement(logged_in, c["id"], start=TODAY, expiry=TODAY - timedelta(days=1))
    assert r.status_code == 422
    assert "on or after the start date" in r.json()["message"]


def test_protected_terms_api_only_returns_masked(logged_in):
    c = logged_in.post("/api/creators", json={"name": "X"}).json()
    r = logged_in.post(f"/api/creators/{c['id']}/protected-terms", json={"term": "Noah", "kind": "child_name"})
    assert r.status_code == 201
    assert "Noah" not in r.text
    assert logged_in.get(f"/api/creators/{c['id']}/protected-terms").json()[0]["masked"] == "N***"


def test_cross_site_post_is_blocked(logged_in):
    r = logged_in.post("/api/creators", json={"name": "Evil"}, headers={"Origin": "https://evil.example"})
    assert r.status_code == 403


def test_web_pages_render_and_file_downloads(logged_in):
    r = logged_in.post("/creators", data={"name": "Tiny Chefs", "contact_email": ""}, follow_redirects=False)
    assert r.status_code == 303
    cid = int(r.headers["location"].rsplit("/", 1)[1])
    page = logged_in.get(f"/creators/{cid}")
    assert page.status_code == 200 and "Tiny Chefs" in page.text and "Blocked" in page.text

    r = logged_in.post(f"/creators/{cid}/agreements",
                       files={"file": ("signed.pdf", PDF, "application/pdf")},
                       data={"allowed_platforms": ["tiktok"], "posting_mode": "our_accounts",
                             "revenue_share_creator_pct": "25",
                             "start_date": (TODAY - timedelta(days=1)).isoformat(),
                             "expiry_date": (TODAY + timedelta(days=3650)).isoformat()},
                       follow_redirects=True)
    assert "Agreement #" in r.text and "saved" in r.text and "Cleared" in r.text

    assert "Tiny Chefs" in logged_in.get("/").text
    assert "agreement.created" in logged_in.get("/audit").text
    f = logged_in.get("/agreements/1/file")
    assert f.status_code == 200 and f.content == PDF


def test_web_form_errors_show_message(logged_in):
    r = logged_in.post("/creators", data={"name": "Y"}, follow_redirects=False)
    cid = r.headers["location"].rsplit("/", 1)[1]
    r = logged_in.post(f"/creators/{cid}/agreements",
                       files={"file": ("x.exe", b"MZ....", "application/octet-stream")},
                       data={"allowed_platforms": ["youtube"], "posting_mode": "our_accounts",
                             "revenue_share_creator_pct": "25", "start_date": TODAY.isoformat(),
                             "expiry_date": TODAY.isoformat()})
    assert "must be a PDF, PNG or JPEG" in r.text
