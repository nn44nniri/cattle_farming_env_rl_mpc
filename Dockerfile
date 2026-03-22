FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake libsqlite3-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/cattle_farming_env_rl_mpc
COPY . /opt/cattle_farming_env_rl_mpc
RUN rm -rf build && cmake -S . -B build && cmake --build build -j"$(nproc)"
RUN chmod +x /opt/cattle_farming_env_rl_mpc/docker-entrypoint.sh
ENTRYPOINT ["/opt/cattle_farming_env_rl_mpc/docker-entrypoint.sh"]
CMD ["service", "--settings", "config/settings.json", "--mode", "run"]
