#!/usr/bin/env bash
set -euo pipefail
cd /opt/cattle_farming_env_rl_mpc
exec ./build/cattle_farming_env_rl_mpc "$@"
