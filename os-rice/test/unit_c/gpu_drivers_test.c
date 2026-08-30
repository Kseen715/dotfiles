/* test/unit_c/gpu_drivers_test.c -- which driver stack modules/gpu-drivers.c
 * installs for which chip.
 *
 * This module has the widest blast radius of any in the tree, and the failure
 * is not "a package is missing" -- it is a box that will not start X. NVIDIA
 * alone has six live driver branches, each supporting a disjoint set of
 * generations, and installing the wrong one produces a machine that boots to a
 * black screen. So the table is asserted generation by generation, by name.
 *
 * The chip CODENAME is what routes: lspci reports "GP104 [GeForce GTX 1080]",
 * detection takes "GP104", and the module maps that to a driver family. Which
 * makes the codename table the thing that has to be right, and the scenarios
 * below are mostly one real card each.
 *
 * Hermetic: lspci is a stub, so the GPU is a property of the scenario; sudo
 * logs the install without running it, so no package manager and no GPU is
 * touched.
 *
 * WHY THE NEGATIVE ASSERTIONS MATTER MORE THAN THE POSITIVE ONES
 *
 * Installing an extra package is usually harmless. Not here: `vulkan-radeon`
 * on pre-GCN hardware installs a driver the card cannot run, and RADV then
 * advertises a Vulkan device that fails on first use. Every "gets no Vulkan"
 * line below is a real hardware constraint, not tidiness.
 *
 * Replaces test/unit/gpu_drivers.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

/* publish_gpu_facts -- run detection and put OSR_GPU_* into the environment.
 *
 * `osr detect gpu` PRINTS shell assignments; the runner evals them so every
 * module downstream inherits the facts. A test driving one module on its own
 * has to do that threading itself, and this is it. */
static void publish_gpu_facts(void) {
    static const char *const vars[] = {
        "OSR_GPU_VENDOR", "OSR_GPU_DEVICES", "OSR_GPU_COUNT", NULL
    };
    const char *out;
    int i;

    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "detect", "gpu", (const char *)NULL);
    out = osr_sb_capture(&sb);
    for (i = 0; vars[i] != NULL; i++) {
        HStr needle, value;
        const char *at;
        hs_init(&needle);
        hs_init(&value);
        hs_add(&needle, vars[i]);
        hs_add(&needle, "='");
        at = strstr(out, hs_text(&needle));
        if (at != NULL) {
            const char *p = at + strlen(hs_text(&needle));
            while (*p != '\0' && *p != '\'') hs_addc(&value, *p++);
        }
        osr_sb_env(&sb, vars[i], hs_text(&value));
        hs_free(&needle);
        hs_free(&value);
    }
}

/* gpu -- one card, as lspci would report it, then run the module.
 *
 * `device` is lspci's device field (the codename, with the marketing name in
 * brackets when lspci knows one) and `vendor` its vendor field. The module
 * reads the facts detection produced, so detection runs first. */
static void gpu(const char *device, const char *vendor) {
    HStr body;
    hs_init(&body);
    hs_add(&body, "printf '01:00.0 \"VGA compatible controller\" \"");
    hs_add(&body, vendor);
    hs_add(&body, "\" \"");
    hs_add(&body, device);
    hs_add(&body, "\" -ra1 \"Sub\" \"Device 1\"\\n'\n");
    osr_sb_stub_body(&sb, "lspci", hs_text(&body));
    hs_free(&body);

    publish_gpu_facts();
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "module", "run", "gpu-drivers", (const char *)NULL);
}

static void installs(const char *pkg, const char *label) {
    osr_assert_log(&sb, pkg, label);
}
static void skips(const char *pkg, const char *label) {
    osr_refute_log(&sb, pkg, label);
}

int main(void) {
    osr_sb_init(&sb);

    osr_sb_env(&sb, "OSR_PKG", "pacman");
    osr_sb_env(&sb, "OSR_DISTRO", "arch");
    osr_sb_env(&sb, "OSR_ID_LIKE", "");
    osr_sb_env(&sb, "OSR_CODENAME", "");
    osr_sb_env(&sb, "OSR_VERSION_ID", "");
    osr_sb_env(&sb, "OSR_INIT", "systemd");

    /* Nothing is installed, so every scenario takes the install path rather
     * than an idempotency skip; the install itself only logs. */
    osr_sb_stub_body(&sb, "pacman",
        "[ \"$1\" = \"-Q\" ] && exit 1\n"
        "printf 'pacman %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    /* The legacy NVIDIA branches are aur: rows, which run as the user through
     * the helper rather than through sudo -- so the helper logs for itself. */
    osr_sb_stub_body(&sb, "paru",
        "printf 'paru %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "dpkg", "exit 1\n");
    osr_sb_stub_body(&sb, "apt-mark", "exit 0\n");
    osr_sb_stub_body(&sb, "apt-get",
        "printf 'apt-get %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");

    /* ================================================================
     * 1. NVIDIA -- six branches, each for a disjoint set of generations
     *
     * The open driver needs a GSP, which is Turing and newer. Everything
     * older is on a frozen legacy branch, and the branches do not overlap:
     * a Pascal card on nvidia-open-dkms does not fall back to something that
     * works, it fails to load and X does not start.
     * ================================================================ */
    gpu("AD102 [GeForce RTX 4090]", "NVIDIA Corporation");
    installs("nvidia-open-dkms", "Ada gets the open driver");

    gpu("GP104 [GeForce GTX 1080]", "NVIDIA Corporation");
    installs("nvidia-570xx-dkms", "Pascal gets the 570xx branch");
    skips("nvidia-open-dkms",
        "Pascal never gets the open driver -- it has no GSP to run it on");

    gpu("GM204 [GeForce GTX 970]", "NVIDIA Corporation");
    installs("nvidia-570xx-dkms", "Maxwell gets the 570xx branch");

    gpu("GK104 [GeForce GTX 680]", "NVIDIA Corporation");
    installs("nvidia-470xx-dkms", "Kepler gets the 470xx branch");

    gpu("GF114 [GeForce GTX 560 Ti]", "NVIDIA Corporation");
    installs("nvidia-390xx-dkms", "Fermi gets the 390xx branch");
    skips("vulkan-icd-loader",
        "Fermi gets no Vulkan -- the 390xx branch never supported it");

    gpu("G92 [GeForce 9800 GT]", "NVIDIA Corporation");
    installs("nvidia-340xx-dkms", "Tesla gets the 340xx branch");

    gpu("NV43 [GeForce 6600 GT]", "NVIDIA Corporation");
    installs("xf86-video-nouveau",
        "Curie is older than every proprietary branch and falls back to nouveau");

    /* A card newer than the installed lspci's device database reads as an
     * unnamed chip. Defaulting to the CURRENT driver is the safe direction:
     * a legacy branch on new hardware cannot work, where the current branch on
     * hardware it does not know at least might. */
    gpu("Device 2c05", "NVIDIA Corporation");
    installs("nvidia-open-dkms",
        "an NVIDIA chip lspci cannot name defaults to the current driver, "
        "never to a legacy branch");

    /* ================================================================
     * 2. AMD -- amdgpu, GCN 1-2, TeraScale, r300, amber
     * ================================================================ */
    gpu("Navi 22 [Radeon RX 6700 XT]", "Advanced Micro Devices, Inc. [AMD/ATI]");
    installs("vulkan-radeon", "Navi gets RADV");
    installs("xf86-video-amdgpu", "Navi gets the amdgpu DDX");

    gpu("TAHITI [Radeon HD 7970]", "Advanced Micro Devices, Inc. [AMD/ATI]");
    installs("xf86-video-ati",
        "GCN1 also gets the radeon DDX, which is its kernel default");
    installs("vulkan-radeon", "GCN1 is still new enough for RADV");

    gpu("CAYMAN [Radeon HD 6970]", "Advanced Micro Devices, Inc. [AMD/ATI]");
    installs("xf86-video-ati", "TeraScale gets the radeon DDX");
    skips("vulkan-radeon", "TeraScale gets no Vulkan -- RADV starts at GCN");

    /* For the whole TeraScale MOBILE line lspci prints the board codename
     * rather than the ASIC one: "Whistler" is a Turks. With no row for those
     * names the chip classified as Unknown and fell through to the GCN branch,
     * which installs RADV on hardware RADV cannot drive. */
    gpu("Whistler [Radeon HD 6730M/6770M/7690M XT]",
        "Advanced Micro Devices, Inc. [AMD/ATI]");
    installs("xf86-video-ati",
        "a TeraScale MOBILE board codename is recognised as TeraScale");
    skips("vulkan-radeon",
        "and so it gets no Vulkan either -- the mobile names are the ones that "
        "used to fall through to the GCN branch");

    gpu("Park [Mobility Radeon HD 5430]", "Advanced Micro Devices, Inc. [AMD/ATI]");
    skips("vulkan-radeon", "an Evergreen mobile codename gets no Vulkan");

    gpu("RV370 [Radeon X600]", "Advanced Micro Devices, Inc. [AMD/ATI]");
    installs("mesa ", "the r300 era is still served by mainline mesa");
    skips("mesa-amber", "the r300 era is not old enough for amber");

    gpu("RV200 [Radeon 7500]", "Advanced Micro Devices, Inc. [AMD/ATI]");
    installs("mesa-amber",
        "R100/R200 gets mesa-amber, the branch that kept the classic drivers");

    /* ================================================================
     * 3. Intel -- iris, crocus, amber
     * ================================================================ */
    gpu("Alder Lake-P GT2 [Iris Xe Graphics]", "Intel Corporation");
    installs("vulkan-intel", "modern Intel gets ANV");
    installs("intel-media-driver", "modern Intel gets the iHD VA-API driver");

    gpu("3rd Gen Core processor Graphics Controller", "Intel Corporation");
    installs("libva-intel-driver", "Ivy Bridge gets the i965 VA-API driver");
    skips("intel-media-driver",
        "Ivy Bridge does not get iHD -- that driver starts at Broadwell");

    gpu("82945G/GZ Integrated Graphics Controller", "Intel Corporation");
    installs("mesa-amber", "gen3 Intel gets mesa-amber");

    /* ================================================================
     * 4. Virtual GPUs
     * ================================================================ */
    gpu("SVGA II Adapter", "VMware");
    installs("open-vm-tools", "a VMware adapter gets the guest tools");

    gpu("Virtio GPU", "Red Hat, Inc.");
    installs("vulkan-virtio", "a virtio GPU gets the venus driver");

    /* ================================================================
     * 5. A hybrid laptop -- both cards served in one run
     *
     * Serving only the first would leave the machine with a working iGPU and
     * an unusable dGPU, or the reverse, with nothing saying which.
     * ================================================================ */
    osr_sb_stub_body(&sb, "lspci",
        "cat <<'EOF'\n"
        "00:02.0 \"VGA compatible controller\" \"Intel Corporation\" "
        "\"Raptor Lake-S GT1 [UHD Graphics 770]\" -r04 \"Sub\" \"Device 1\"\n"
        "01:00.0 \"3D controller\" \"NVIDIA Corporation\" "
        "\"AD107M [GeForce RTX 4060 Max-Q]\" -ra1 \"Sub\" \"Device 2\"\n"
        "EOF\n");
    publish_gpu_facts();
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "module", "run", "gpu-drivers", (const char *)NULL);
    installs("vulkan-intel", "a hybrid laptop's Intel iGPU is served");
    installs("nvidia-open-dkms", "and its NVIDIA dGPU is served in the same run");

    /* ================================================================
     * 6. Off Arch
     *
     * The logical names above are Arch's; on apt the pkgmap resolves them to
     * the Debian ones. What matters is that the module RUNS rather than
     * skipping -- gating it on the package manager is exactly the smearing of
     * distro variance the pkgmap exists to prevent.
     * ================================================================ */
    osr_sb_env(&sb, "OSR_PKG", "apt");
    osr_sb_env(&sb, "OSR_DISTRO", "ubuntu");
    osr_sb_env(&sb, "OSR_ID_LIKE", "debian");
    osr_sb_env(&sb, "OSR_CODENAME", "noble");
    osr_sb_env(&sb, "OSR_VERSION_ID", "24.04");
    gpu("Navi 33 [Radeon RX 7600]", "Advanced Micro Devices, Inc. [AMD/ATI]");
    installs("mesa-vulkan-drivers",
        "off Arch the module still runs, and the pkgmap turns the Arch name "
        "for RADV into the Debian one");

    osr_sb_free(&sb);
    return osr_finish();
}
