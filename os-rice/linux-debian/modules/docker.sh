info "Installing Docker..."

install_pkg_apt ca-certificates curl

# Detect the OS ID (debian, ubuntu, …) to pick the correct Docker repo URL
DOCKER_OS_ID=$(. /etc/os-release && echo "$ID")
if [ -z "$DOCKER_OS_ID" ]; then
    error "Failed to detect OS ID from /etc/os-release"
fi
DOCKER_REPO_URL="https://download.docker.com/linux/$DOCKER_OS_ID"
info "Docker repository URL: $DOCKER_REPO_URL"

# Add Docker's official GPG key (kept as ASCII armor, referenced by Signed-By)
install_gpg_key_apt \
    "$DOCKER_REPO_URL/gpg" \
    "docker.asc" \
    "/etc/apt/keyrings"

# Add Docker's repository in DEB822 format
install_deb822_repo_apt \
    "docker.sources" \
    "$DOCKER_REPO_URL" \
    "stable" \
    "/etc/apt/keyrings/docker.asc"

# Install Docker engine and plugins
install_pkg_apt \
    docker-ce \
    docker-ce-cli \
    containerd.io \
    docker-buildx-plugin \
    docker-compose-plugin

# Create docker group if it doesn't exist
if ! getent group docker &>/dev/null; then
    info "Creating docker group..."
    trace groupadd docker
    check_error $? "Failed to create docker group"
else
    info "docker group already exists -- skipping"
fi

# Add the delevated user to the docker group
if id -nG "$DELEVATED_USER" | grep -qw docker; then
    info "$DELEVATED_USER is already in the docker group -- skipping"
else
    info "Adding $DELEVATED_USER to docker group..."
    trace usermod -aG docker "$DELEVATED_USER"
    check_error $? "Failed to add $DELEVATED_USER to docker group"
    warning "Group change takes effect on next login (re-login or run: newgrp docker)"
fi