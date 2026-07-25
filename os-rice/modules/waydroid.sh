# modules/waydroid.sh — Waydroid (Android in a container) + GApps image + the
# ARM translation layer for the detected CPU. POSIX port of .../modules/waydroid.sh.
# Needs a real kernel (binder), systemd, and network -> validated on hardware,
# not CI (§9). Available module (not in the default rice.list).
run_step "Installing Waydroid (AUR)" pkg_install waydroid waydroid-image-gapps

run_step "Initializing Waydroid (GApps)" as_root waydroid init -s GAPPS

_wsr="${TMPDIR:-/tmp}/waydroid_script"
install_or_update_git_repo waydroid_script \
    https://github.com/casualsnek/waydroid_script.git "$_wsr" --depth 1
run_step "Setting up waydroid_script venv" as_user python3 -m venv --clear "$_wsr/venv"
run_step "Installing waydroid_script deps" as_user "$_wsr/venv/bin/pip" install -r "$_wsr/requirements.txt"

case "${OSR_CPU_VENDOR:-}" in
    GenuineIntel) run_step "Installing libhoudini (Intel)" as_user "$_wsr/venv/bin/python" "$_wsr/main.py" install libhoudini ;;
    AuthenticAMD) run_step "Installing libndk (AMD)"       as_user "$_wsr/venv/bin/python" "$_wsr/main.py" install libndk ;;
    *)            warn "unsupported CPU vendor '${OSR_CPU_VENDOR:-}' for Waydroid ARM libs - skipping" ;;
esac

enable_service waydroid-container
