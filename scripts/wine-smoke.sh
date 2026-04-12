#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIN_DIR="${1:-${ROOT_DIR}/build-win}"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "Missing required command: $1" >&2; exit 1; }
}

need_cmd wine

if [[ ! -f "${WIN_DIR}/openv.exe" ]]; then
  echo "Missing ${WIN_DIR}/openv.exe" >&2
  exit 1
fi
if [[ ! -f "${WIN_DIR}/openv-facecap.exe" ]]; then
  echo "Missing ${WIN_DIR}/openv-facecap.exe" >&2
  exit 1
fi

echo "Running Wine smoke test from ${WIN_DIR}"
(
  cd "${WIN_DIR}"
  export OPENV_FACECAP_CMD="openv-facecap.exe"
  timeout 20s wine ./openv.exe || true
)

echo "Smoke test done. Check console output for missing DLL errors and startup warnings."
