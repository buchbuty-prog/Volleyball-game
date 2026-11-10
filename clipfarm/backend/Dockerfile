FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
# ffmpeg is needed from step 3 (transcription/cutting); installed now so the image doesn't change later.
RUN apt-get update && apt-get install -y --no-install-recommends ffmpeg && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY requirements.txt requirements-dev.txt ./
RUN pip install --no-cache-dir -r requirements-dev.txt
COPY . .

RUN useradd --create-home clipfarm && chown -R clipfarm /app
USER clipfarm

EXPOSE 8000
CMD ["sh", "-c", "alembic upgrade head && uvicorn clipfarm.main:app --host 0.0.0.0 --port 8000"]
