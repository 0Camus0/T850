#!/usr/bin/env bash
set -euo pipefail

APT_ROOT="${1:-/etc/apt}"
shopt -s nullglob
for source in "${APT_ROOT}/sources.list.d/"*.sources; do
  if grep -Eq '^Suites:.*(^|[[:space:]])bullseye-security(-debug)?([[:space:]]|$)' "${source}"; then
    echo "[T850] Retired Bullseye security feed in unsupported deb822 source: ${source}" >&2
    exit 1
  fi
done

for source in "${APT_ROOT}/sources.list" "${APT_ROOT}/sources.list.d/"*.list; do
  [[ -f "${source}" ]] || continue
  if grep -Eq '^[[:space:]]*deb(-src)?[[:space:]].*[[:space:]]bullseye-security(-debug)?([[:space:]]|$)' "${source}"; then
    echo "[T850] Retiring discontinued Bullseye LTS feeds in ${source}; APT verification remains enabled."
    sed -Ei '/^[[:space:]]*deb(-src)?[[:space:]].*[[:space:]]bullseye-security(-debug)?([[:space:]]|$)/s/^/# T850: Bullseye LTS ended 2026-08-31: /' "${source}"
  fi
done