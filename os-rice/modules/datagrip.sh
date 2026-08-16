# session: x11+wayland
# modules/datagrip.sh — DataGrip, JetBrains' database IDE, from the vendor
# tarball (jetbrains.com/datagrip/download, "Linux" tab). There is no repo and
# no deb/rpm: the .tar.gz is the only route JetBrains publishes for Linux, so
# every target takes the same one (any.map -> source:provide_datagrip). The tree
# is self-contained (it carries its own JBR), and it lands in /opt/datagrip with
# a /usr/local/bin/datagrip launcher and a menu entry using the icon out of the
# tarball itself.
#
# Why this calls provide_datagrip directly instead of `pkg_install datagrip`:
# the source: provider's idempotency probe is `command -v datagrip` (§4), which
# is satisfied by ANY installed version - so a rice listing `datagrip` installs
# it once and then skips forever. An IDE that ships a new build every few weeks
# needs version, not presence, to be the test. The builder does exactly that
# (compares /opt/datagrip's product-info.json against JetBrains' release feed,
# same shape as provide_chafa), which makes `osr module datagrip` the upgrade
# path: it downloads only when the installed tree is behind, and replaces it in
# place when it is.
#
# Copies of DataGrip this module does NOT own - JetBrains Toolbox, a snap, a
# flatpak, a distro/AUR package, a hand-unpacked /opt/DataGrip-* - are reported
# by the builder and left alone (§5, G2). They matter because a Toolbox or snap
# launcher earlier on PATH is the one that opens when the user types `datagrip`;
# remove those by hand if this should be the only DataGrip on the box.
#
# Settings are deliberately not layered here (§5): DataGrip keeps its config in
# ~/.config/JetBrains/DataGrip<version>/ as XML the IDE rewrites on exit, and it
# holds data source definitions - user data, not rice config.

run_step "Installing DataGrip" provide_datagrip

# The license is per-user and interactive (JetBrains Account or an activation
# code), so the first launch will ask; nothing here can pre-seed it.
info "DataGrip installed to /opt/datagrip - run 'datagrip' or use the menu entry; the first launch asks for a license"
