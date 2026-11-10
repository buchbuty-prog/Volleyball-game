from pathlib import Path
from urllib.parse import urlsplit

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, RedirectResponse
from fastapi.staticfiles import StaticFiles
from sqlalchemy import text
from starlette.middleware.sessions import SessionMiddleware

from . import __version__
from .api.routes import router as api_router
from .auth import ApiAuthRequired, LoginRequired
from .config import get_settings
from .db import get_sessionmaker
from .rights import RightsDenied
from .rights.service import RegistryError
from .storage import StorageError
from .web.routes import router as web_router

UNSAFE = {"POST", "PUT", "PATCH", "DELETE"}


def create_app() -> FastAPI:
    settings = get_settings()
    app = FastAPI(title=settings.app_name, version=__version__)

    @app.middleware("http")
    async def same_origin_guard(request: Request, call_next):
        # CSRF defence on top of SameSite=Lax cookies: reject cross-site writes.
        if request.method in UNSAFE:
            origin = request.headers.get("origin") or request.headers.get("referer")
            if origin and urlsplit(origin).netloc != request.headers.get("host"):
                return JSONResponse({"error": "cross_origin", "message": "Cross-site request blocked"},
                                    status_code=403)
        response = await call_next(request)
        response.headers.setdefault("X-Frame-Options", "DENY")
        response.headers.setdefault("Referrer-Policy", "same-origin")
        return response

    app.add_middleware(SessionMiddleware, secret_key=settings.secret_key, same_site="lax",
                       https_only=settings.session_cookie_secure,
                       max_age=settings.session_max_age_hours * 3600, session_cookie="clipfarm")

    @app.exception_handler(LoginRequired)
    async def _login(request: Request, exc: LoginRequired):
        return RedirectResponse("/login", status_code=303)

    @app.exception_handler(ApiAuthRequired)
    async def _api_auth(request: Request, exc: ApiAuthRequired):
        return JSONResponse({"error": "unauthenticated", "message": "Log in first"}, status_code=401)

    @app.exception_handler(RightsDenied)
    async def _denied(request: Request, exc: RightsDenied):
        return JSONResponse({"error": "rights_denied", "message": str(exc), "reasons": exc.reasons},
                            status_code=403)

    @app.exception_handler(RegistryError)
    async def _invalid(request: Request, exc: RegistryError):
        return JSONResponse({"error": "invalid", "message": str(exc)}, status_code=422)

    @app.exception_handler(StorageError)
    async def _storage(request: Request, exc: StorageError):
        return JSONResponse({"error": "storage", "message": str(exc)}, status_code=502)

    @app.get("/api/health", include_in_schema=False)
    def health():
        with get_sessionmaker()() as db:
            db.execute(text("SELECT 1"))
        return {"ok": True, "version": __version__}

    app.mount("/static", StaticFiles(directory=str(Path(__file__).parent / "web" / "static")),
              name="static")
    app.include_router(api_router)
    app.include_router(web_router)
    return app


app = create_app()
