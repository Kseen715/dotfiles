# test/ref/docker_sh_ref.sh — the sh implementation of modules/docker.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/linux/docker.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/docker.sh — Docker engine. ONE copy, POSIX, distro-agnostic (was
# linux-debian/modules/docker.sh, which added docker.com's apt repo for
# docker-ce). Native-first: install the distro's engine package (`docker.io` on
# Debian/Ubuntu, `moby-engine` on Fedora, `docker` elsewhere - resolved by
# pkgmap), so it updates through the package manager.

run_step "Installing Docker" pkg_install docker

# docker group + membership so OSR_USER can reach the socket without sudo.
# root-for-root needs neither. Tool names differ (shadow's groupadd/usermod vs
# busybox's addgroup), so dispatch on what exists.
_docker_group_exists() {
    if command -v getent >/dev/null 2>&1; then getent group docker >/dev/null 2>&1
    else grep -q '^docker:' /etc/group 2>/dev/null; fi
}
if ! _docker_group_exists; then
    if command -v groupadd >/dev/null 2>&1; then
        run_step "Creating docker group" as_root groupadd docker
    elif command -v addgroup >/dev/null 2>&1; then
        run_step "Creating docker group" as_root addgroup docker
    fi
fi
if [ "$OSR_USER" != root ]; then
    if id -nG "$OSR_USER" 2>/dev/null | grep -qw docker; then
        info "$OSR_USER already in docker group - skipping"
    elif command -v usermod >/dev/null 2>&1; then
        run_step "Adding $OSR_USER to docker group" as_root usermod -aG docker "$OSR_USER"
        warn "docker group change takes effect on next login (or run: newgrp docker)"
    elif command -v addgroup >/dev/null 2>&1; then
        run_step "Adding $OSR_USER to docker group" as_root addgroup "$OSR_USER" docker
        warn "docker group change takes effect on next login (or run: newgrp docker)"
    fi
fi

# Enable the daemon where an init can run it. In a container (no real init /
# cgroups) this can't start dockerd, so degrade to a warning rather than fail
# the run - the daemon is a real-init concern (§9), not an install error.
enable_service docker || warn "could not enable docker service (needs a real init)"
