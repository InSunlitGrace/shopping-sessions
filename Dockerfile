FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ \
    && rm -rf /var/lib/apt/lists/*
COPY cpp/sessions.cpp /src/sessions.cpp
RUN g++ -O3 -std=c++17 -pthread -static-libstdc++ -static-libgcc -o /src/sessions /src/sessions.cpp

FROM debian:bookworm-slim
COPY --from=build /src/sessions /app/sessions
COPY run.sh /app/run.sh
RUN chmod +x /app/run.sh /app/sessions
WORKDIR /app
CMD ["/app/run.sh"]