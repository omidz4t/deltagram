# Telegram application API CMake flags for nix/build-release.sh and
# nix/build-tdesktop.sh. TDESKTOP_API_TEST selects the donor test
# application api_id/api_hash (tdesktop/Telegram/cmake/telegram_options.cmake).
# That is not MTP::Environment::Test and not a TestDC switch.
#
# DELTA_TEL_API_MODE=test|production  (default: test)
# production also requires TDESKTOP_API_ID and TDESKTOP_API_HASH.
# This file must not print those values.

DELTA_TEL_TELEGRAM_API_MODE=test
DELTA_TEL_TELEGRAM_API_NEEDS_CONFIGURE=1
DELTA_TEL_TELEGRAM_API_CMAKE_ARGS=()

_delta_tel_api_restore_xtrace=0

_delta_tel_api_silence_xtrace() {
  _delta_tel_api_restore_xtrace=0
  case $- in *x*) _delta_tel_api_restore_xtrace=1; set +x ;; esac
}

_delta_tel_api_resume_xtrace() {
  if [ "$_delta_tel_api_restore_xtrace" -eq 1 ]; then
    _delta_tel_api_restore_xtrace=0
    set -x
  fi
}

_delta_tel_api_fail() {
  echo "$*" >&2
  _delta_tel_api_resume_xtrace
  return 2
}

_delta_tel_cache_get() {
  # $1 file, $2 CMake cache key. Caller must not print production values.
  local file="$1" key="$2" line=""
  [ -f "$file" ] || return 0
  line=$(grep -E "^${key}:" "$file" 2>/dev/null | head -n 1 || true)
  line=$(printf '%s' "$line" | tr -d '\r')
  printf '%s\n' "${line#*=}"
}

_delta_tel_cache_bool_on() {
  case "$1" in
    ON|on|TRUE|true|YES|yes|1) return 0 ;;
    *) return 1 ;;
  esac
}

_delta_tel_cache_bool_off() {
  case "$1" in
    OFF|off|FALSE|false|NO|no|0) return 0 ;;
    *) return 1 ;;
  esac
}

_delta_tel_load_donor_test_pair() {
  local file="$ROOT/tdesktop/Telegram/cmake/telegram_options.cmake"
  if [ ! -f "$file" ]; then
    _delta_tel_api_fail "Cannot read TDESKTOP_API_TEST defaults from telegram_options.cmake." || return 2
  fi
  _DELTA_TEL_DONOR_TEST_API_ID=$(awk '
    /if \(TDESKTOP_API_TEST\)/ { grab = 1 }
    grab && /set\(TDESKTOP_API_ID / {
      value = $2
      gsub(/[()]/, "", value)
      print value
      exit
    }
  ' "$file")
  _DELTA_TEL_DONOR_TEST_API_HASH=$(awk '
    /if \(TDESKTOP_API_TEST\)/ { grab = 1 }
    grab && /set\(TDESKTOP_API_HASH / {
      value = $2
      gsub(/[()]/, "", value)
      print value
      exit
    }
  ' "$file")
  if [ -z "$_DELTA_TEL_DONOR_TEST_API_ID" ] || [ -z "$_DELTA_TEL_DONOR_TEST_API_HASH" ]; then
    _delta_tel_api_fail "telegram_options.cmake TDESKTOP_API_TEST pair is unreadable." || return 2
  fi
}

_delta_tel_normalize_hash() {
  printf '%s' "$1" | tr 'ABCDEF' 'abcdef'
}

_delta_tel_validate_production_credentials() {
  local id="${TDESKTOP_API_ID-}" hash="${TDESKTOP_API_HASH-}"
  local hash_norm="" donor_hash_norm=""
  if [ -z "$id" ] || [ -z "$hash" ]; then
    _delta_tel_api_fail "production Telegram API mode requires nonempty TDESKTOP_API_ID and TDESKTOP_API_HASH." || return 2
  fi
  case "$id" in
    *[!0-9]*|0*)
      _delta_tel_api_fail "TDESKTOP_API_ID is malformed." || return 2
      ;;
  esac
  if [ "${#id}" -gt 10 ] || [ "$id" -gt 2147483647 ]; then
    _delta_tel_api_fail "TDESKTOP_API_ID is malformed." || return 2
  fi
  case "$hash" in
    *[!0-9a-fA-F]*)
      _delta_tel_api_fail "TDESKTOP_API_HASH is malformed." || return 2
      ;;
  esac
  if [ "${#hash}" -ne 32 ]; then
    _delta_tel_api_fail "TDESKTOP_API_HASH is malformed." || return 2
  fi
  _delta_tel_load_donor_test_pair || return 2
  hash_norm=$(_delta_tel_normalize_hash "$hash")
  donor_hash_norm=$(_delta_tel_normalize_hash "$_DELTA_TEL_DONOR_TEST_API_HASH")
  if [ "$id" = "$_DELTA_TEL_DONOR_TEST_API_ID" ] || [ "$hash_norm" = "$donor_hash_norm" ]; then
    _delta_tel_api_fail "production Telegram API mode rejects the donor TDESKTOP_API_TEST application credentials." || return 2
  fi
}

_delta_tel_cache_matches_request() {
  local cache="$1/CMakeCache.txt"
  local cached_test="" cached_id="" cached_hash=""
  local want_id="${TDESKTOP_API_ID-}" want_hash="${TDESKTOP_API_HASH-}"
  local cached_hash_norm="" want_hash_norm=""
  if [ ! -f "$cache" ]; then
    return 1
  fi
  cached_test=$(_delta_tel_cache_get "$cache" TDESKTOP_API_TEST)
  cached_id=$(_delta_tel_cache_get "$cache" TDESKTOP_API_ID)
  cached_hash=$(_delta_tel_cache_get "$cache" TDESKTOP_API_HASH)
  if [ "$DELTA_TEL_TELEGRAM_API_MODE" = test ]; then
    if ! _delta_tel_cache_bool_on "$cached_test"; then
      return 1
    fi
    if [ -n "$cached_id" ] && [ "$cached_id" != 0 ]; then
      return 1
    fi
    if [ -n "$cached_hash" ]; then
      return 1
    fi
    return 0
  fi
  if ! _delta_tel_cache_bool_off "$cached_test"; then
    return 1
  fi
  cached_hash_norm=$(_delta_tel_normalize_hash "$cached_hash")
  want_hash_norm=$(_delta_tel_normalize_hash "$want_hash")
  if [ "$cached_id" = "$want_id" ] && [ "$cached_hash_norm" = "$want_hash_norm" ]; then
    return 0
  fi
  return 1
}

delta_tel_init_telegram_api() {
  local build_dir="$1"
  if [ -z "${ROOT:-}" ]; then
    echo "telegram-api-config.sh requires ROOT." >&2
    return 2
  fi
  _delta_tel_api_silence_xtrace
  DELTA_TEL_TELEGRAM_API_MODE=${DELTA_TEL_API_MODE:-test}
  DELTA_TEL_TELEGRAM_API_CMAKE_ARGS=()
  DELTA_TEL_TELEGRAM_API_NEEDS_CONFIGURE=1
  case "$DELTA_TEL_TELEGRAM_API_MODE" in
    test|production) ;;
    *)
      _delta_tel_api_fail "DELTA_TEL_API_MODE must be test or production." || return 2
      ;;
  esac
  if [ "$DELTA_TEL_TELEGRAM_API_MODE" = production ]; then
    _delta_tel_validate_production_credentials || return 2
    DELTA_TEL_TELEGRAM_API_CMAKE_ARGS=(-DTDESKTOP_API_TEST=OFF)
    DELTA_TEL_TELEGRAM_API_CMAKE_ARGS+=("-DTDESKTOP_API_ID=${TDESKTOP_API_ID}")
    DELTA_TEL_TELEGRAM_API_CMAKE_ARGS+=("-DTDESKTOP_API_HASH=${TDESKTOP_API_HASH}")
  else
    DELTA_TEL_TELEGRAM_API_CMAKE_ARGS=(-DTDESKTOP_API_TEST=ON -U TDESKTOP_API_ID -U TDESKTOP_API_HASH)
  fi
  if [ ! -f "$build_dir/build.ninja" ] || ! _delta_tel_cache_matches_request "$build_dir"; then
    DELTA_TEL_TELEGRAM_API_NEEDS_CONFIGURE=1
  else
    DELTA_TEL_TELEGRAM_API_NEEDS_CONFIGURE=0
  fi
  echo "Telegram API mode: $DELTA_TEL_TELEGRAM_API_MODE"
  if [ "$DELTA_TEL_TELEGRAM_API_NEEDS_CONFIGURE" -eq 1 ] && [ -f "$build_dir/build.ninja" ]; then
    echo "Reconfiguring CMake Telegram API settings"
  fi
  _delta_tel_api_resume_xtrace
}

delta_tel_cmake_configure() {
  local build_dir="$1"
  shift
  local restore_xtrace=0 st=0
  case $- in *x*) restore_xtrace=1; set +x ;; esac
  if [ "$DELTA_TEL_TELEGRAM_API_NEEDS_CONFIGURE" -eq 1 ]; then
    cmake -S "$ROOT/tdesktop" -B "$build_dir" -G Ninja \
      "$@" "${DELTA_TEL_TELEGRAM_API_CMAKE_ARGS[@]}"
    st=$?
  fi
  if [ "$restore_xtrace" -eq 1 ]; then set -x; fi
  return $st
}
