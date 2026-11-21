FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ \
    && rm -rf /var/lib/apt/lists/*
COPY cpp/sessions.cpp /src/sessions.cpp
RUN g++ -O2 -std=c++17 -o /src/sessions /src/sessions.cpp

FROM python:3.12-slim-bookworm
RUN useradd --uid 1000 --no-create-home --home-dir /scratch app
COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt
COPY pipeline/driver.py /app/pipeline/driver.py
COPY --from=build /src/sessions /app/pipeline/sessions
COPY run.sh /app/run.sh
RUN chmod +x /app/run.sh /app/pipeline/sessions
ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1
WORKDIR /app
CMD ["/app/run.sh"]
