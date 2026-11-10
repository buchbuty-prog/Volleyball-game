# ClipFarm

Turns long videos from creators **who gave written permission** into short clips
for YouTube Shorts, Instagram Reels and TikTok. Built for kids' content, so child
safety is enforced in code, and **nothing publishes without human review**.

Pipeline: rights registry → file intake → transcription & moment finding → cutting
& captions → review queue → publishing → analytics.

| Step | Status |
|---|---|
| 1. Rights registry (creators, agreements, gate, audit log, protected names) | ✅ done |
| 2. File intake + ranking popular videos | next |
| 3. Transcription & moment finding | |
| 4. Cutting & captions | |
| 5. Review queue + safety blocks | |
| 6. Publishing (YouTube / Instagram / TikTok) | |
| 7. Analytics & re-weighting | |

Accounts, API keys and platform approvals: **[docs/SETUP.md](docs/SETUP.md)**.

## Run it with Docker

```bash
cd clipfarm
cp .env.example .env
docker compose run --rm api python -m clipfarm.cli gen-keys   # paste output into .env
# also set POSTGRES_PASSWORD and S3_SECRET_KEY in .env
docker compose up --build -d
docker compose exec api python -m clipfarm.cli create-user yourname
```

Open <http://localhost:8000> and log in. MinIO's console is on <http://localhost:9001>.

## Run tests

```bash
docker compose run --rm api pytest                 # SQLite, fast
docker compose run --rm -e TEST_DATABASE_URL=postgresql+psycopg://clipfarm:<pw>@postgres:5432/clipfarm_test api pytest
```
For the Postgres run, create `clipfarm_test` first (`docker compose exec postgres createdb -U clipfarm clipfarm_test`).
**It wipes that database.** The Postgres run also checks the append-only audit-log trigger.

Without Docker: `cd backend && pip install -r requirements-dev.txt && pytest`.

## How the rights gate works

`clipfarm/rights/gate.py` is the single check every pipeline step calls. Content from
a creator is allowed for a platform on a day only if the creator is active and has an
agreement that is not revoked, has a signed file uploaded, is within its start and
expiry dates (both inclusive), and lists that platform. Every check made by the
pipeline is written to the audit log. Denials are logged in their own transaction,
so they're kept even when the step fails.

Agreement terms can't be edited. To change terms, add a new agreement and revoke the
old one, so the history of what was permitted when is never lost. Postgres refuses
`UPDATE`/`DELETE` on the audit log.

## Layout

```
backend/clipfarm/
  config.py db.py crypto.py storage.py audit.py auth.py main.py cli.py
  models/        SQLAlchemy models
  rights/        gate.py (the check), service.py (create/change records)
  api/           JSON API under /api
  web/           admin UI (server-rendered)
backend/migrations/  Alembic
backend/tests/
```
