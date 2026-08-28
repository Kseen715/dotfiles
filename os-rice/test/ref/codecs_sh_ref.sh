# test/ref/codecs_sh_ref.sh — the sh implementation of modules/codecs.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/codecs.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/codecs.sh — GStreamer stack, ffmpeg and hardware video decode
# (i3-sugg §3.7). GStreamer is what GTK apps, browsers-via-portal and most
# desktop media players decode through; ffmpeg is what everything else shells
# out to.
#
# The VA-API driver is picked from the detected GPU vendor (OSR_GPU_VENDOR, set
# by lib/detect.sh) — installing all of them works but drags in three vendors'
# userspace for nothing. Verify afterwards with `vainfo` / `vdpauinfo`.

run_step "Installing GStreamer + ffmpeg" pkg_install \
    gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly \
    gst-libav gst-plugin-pipewire ffmpeg

_hw="libva libvdpau vulkan-icd-loader libva-utils vdpauinfo"
_hw_seen=""
# OSR_GPU_VENDOR is a space-separated vendor list ("Intel NVIDIA" on a hybrid
# laptop), so iterate rather than match one value (mirrors modules/gpu-drivers.sh).
for _v in ${OSR_GPU_VENDOR:-}; do
    case "$_v" in
        Intel)  _hw="$_hw intel-media-driver libva-intel-driver"; _hw_seen=1 ;;
        AMD)    _hw="$_hw libva-mesa-driver mesa-vdpau";           _hw_seen=1 ;;
        NVIDIA) _hw="$_hw nvidia-vaapi-driver libva-mesa-driver";  _hw_seen=1 ;;
    esac
done
if [ -z "$_hw_seen" ]; then
    _hw="$_hw libva-mesa-driver mesa-vdpau"
    info "no Intel/AMD/NVIDIA GPU detected - installing the mesa VA-API/VDPAU drivers"
fi
# shellcheck disable=SC2086  # intentional word-split into a package list
run_step "Installing hardware video decode" pkg_install $_hw
