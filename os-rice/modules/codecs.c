/* modules/codecs.c -- GStreamer stack, ffmpeg and hardware video decode
 * (i3-sugg §3.7). GStreamer is what GTK apps, browsers-via-portal and most
 * desktop media players decode through; ffmpeg is what everything else shells
 * out to.
 *
 * The VA-API driver is picked from the detected GPU vendor (OSR_GPU_VENDOR, set
 * by lib/detect.sh) — installing all of them works but drags in three vendors'
 * userspace for nothing. Verify afterwards with `vainfo` / `vdpauinfo`.
 * OSR_GPU_VENDOR is a space-separated vendor list ("Intel NVIDIA" on a hybrid
 * laptop), so iterate rather than match one value (mirrors modules/gpu-drivers.sh).
 * shellcheck disable=SC2086  # intentional word-split into a package list
 *
 * Was modules/codecs.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

int osrm_codecs(void) {
    static const char *const gst[] = {
        "gstreamer", "gst-plugins-base", "gst-plugins-good", "gst-plugins-bad",
        "gst-plugins-ugly", "gst-libav", "gst-plugin-pipewire", "ffmpeg", NULL
    };
    /* The vendor-neutral half, always installed. */
    static const char *const common[] = {
        "libva", "libvdpau", "vulkan-icd-loader", "libva-utils", "vdpauinfo", NULL
    };
    const char *hw[16];
    size_t n = 0, i;
    const char *vendors = env_str("OSR_GPU_VENDOR", "");
    int seen = 0;
    int ok;

    ok = osr_pkg_install_step("Installing GStreamer + ffmpeg", gst);

    for (i = 0; common[i] != NULL; i++) hw[n++] = common[i];
    /* OSR_GPU_VENDOR is a word list: a laptop with switchable graphics has two,
     * and both drivers are wanted. */
    {
        Str word;
        const char *p = vendors;
        str_init(&word);
        while (*p != '\0') {
            str_reset(&word);
            while (is_space(*p)) p++;
            while (*p != '\0' && !is_space(*p)) str_addc(&word, *p++);
            if (word.len == 0) continue;
            if (strcmp(str_text(&word), "Intel") == 0) {
                hw[n++] = "intel-media-driver"; hw[n++] = "libva-intel-driver"; seen = 1;
            } else if (strcmp(str_text(&word), "AMD") == 0) {
                hw[n++] = "libva-mesa-driver"; hw[n++] = "mesa-vdpau"; seen = 1;
            } else if (strcmp(str_text(&word), "NVIDIA") == 0) {
                hw[n++] = "nvidia-vaapi-driver"; hw[n++] = "libva-mesa-driver"; seen = 1;
            }
        }
        str_free(&word);
    }
    if (!seen) {
        /* mesa covers everything else that has a VA-API/VDPAU path at all, and
         * costs nothing where the hardware has none. */
        hw[n++] = "libva-mesa-driver"; hw[n++] = "mesa-vdpau";
        osr_info("no Intel/AMD/NVIDIA GPU detected - installing the mesa VA-API/VDPAU drivers");
    }
    hw[n] = NULL;
    return osr_pkg_install_step("Installing hardware video decode", hw) && ok;
}
