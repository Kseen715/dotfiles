/* modules/gpu-drivers.c -- GPU drivers + Vulkan/OpenCL/VA-API stack for every
 * detected GPU, across every generation the Arch repos and AUR still carry:
 * NVIDIA Blackwell..Curie, AMD Navi..R100, Intel Xe..gen3, plus the VM vendors.
 *
 * Vendor alone can't pick a driver -- the same "NVIDIA" needs nvidia-open-dkms on
 * Turing+, a frozen AUR branch on Maxwell/Kepler/Fermi/Tesla, and nouveau below
 * that. So each vendor's chip codename (OSR_GPU_DEVICES, §7) goes through a
 * family classifier, and the family picks the package set.
 *
 * Package names below are Arch's (and the legacy NVIDIA branches are AUR rows),
 * but the module runs everywhere: the family matrix is distro-independent, so on
 * a non-pacman host it fails loudly on the first missing package instead of
 * silently installing no driver at all. Fix a break by adding the distro's rows
 * to lib/pkgmap/<mgr>.map -- the logical names here stay put.
 *
 * dkms + kernel headers come from the dkms module and paru from the paru module:
 * list both BEFORE this one (manifest order is the dependency graph, §4). No
 * explicit `dkms install` -- Arch's dkms package ships the alpm hooks that build
 * every module on install and on kernel upgrade.
 *
 * Hardware-dependent and NOT container-testable (§9); the classifiers are pure
 * functions, so test/unit/gpu_drivers.sh covers the matrix without a GPU.
 *
 * Was modules/gpu-drivers.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/cmds.h"
#include "../lib/common.h"

#include <fnmatch.h>
#include <stddef.h>

/* Vulkan loader + tools: every branch that has a Vulkan driver at all wants
 * these. Spelled once, spliced into each list below where the sh module wrote
 * `$_gpu_vk`. */
#define VK "vulkan-icd-loader", "lib32-vulkan-icd-loader", "vulkan-tools"

/* One row of a family table: shell `case` alternatives, '|'-separated, and the
 * family they name. First match wins, so ORDER IS THE SPECIFICATION -- Intel's
 * "2nd Generation Core Processor" must be tested before the gen5 "Core
 * Processor" it contains. */
typedef struct { const char *patterns; const char *family; } FamilyRow;

/* classify -- the shell `case` over one chip codename. */
static const char *classify(const FamilyRow *rows, const char *chip) {
    size_t i;
    for (i = 0; rows[i].patterns != NULL; i++) {
        const char *p = rows[i].patterns;
        while (*p != '\0') {
            const char *end = strchr(p, '|');
            size_t n = (end != NULL) ? (size_t)(end - p) : strlen(p);
            char pat[128];
            if (n < sizeof(pat)) {
                memcpy(pat, p, n);
                pat[n] = '\0';
                if (fnmatch(pat, chip, 0) == 0) return rows[i].family;
            }
            if (end == NULL) break;
            p = end + 1;
        }
    }
    return "Unknown";
}

/* Codename -> family. Both NVIDIA naming schemes appear in the wild: modern
 * lspci prints "AD102"/"GA104", older/quirky tables print the internal
 * "NV190"/"NVE0". Unknown (incl. empty) falls through to current: a chip too new
 * for this lspci is far likelier than one too old. */
static const FamilyRow NVIDIA[] = {
    { "GB*|NV1[AB]0",         "Blackwell" },
    { "AD*|NV190",            "Ada Lovelace" },
    { "GA*|NV170",            "Ampere" },
    { "TU*|NV160",            "Turing" },
    { "GV*|NV140",            "Volta" },
    { "GP*|NV130",            "Pascal" },
    { "GM*|NV110",            "Maxwell" },
    { "GK*|NVE*",             "Kepler" },
    { "GF*|NVC*",             "Fermi" },
    { "G8*|G9*|GT2*|NV5*",    "Tesla" },
    { "G7*|NV4*",             "Curie" },
    { "NV3*",                 "Rankine" },
    { "NV2*",                 "Kelvin" },
    { "NV1*",                 "Celsius" },
    { "NV0*",                 "Fahrenheit" },
    { NULL, NULL }
};

/* Marketing generation names are useless here (an "RX 6700" and an "RX 580" are
 * both "Radeon RX"); the ASIC codename is what maps to a driver: amdgpu for
 * GCN3+, radeon/r600/r300 gallium below that, and mesa-amber for the
 * pre-shader R100/R200. */
static const FamilyRow AMD[] = {
    { "Navi*|Strix*|Phoenix*|Rembrandt*|Van Gogh*|Raphael*|Granite*", "Navi" },
    { "Vega*|Raven*|Picasso*|Renoir*|Cezanne*|Barcelo*",              "Vega" },
    { "POLARIS*|Polaris*|Ellesmere*|Baffin*|Lexa*|Neo*|Scorpio*",     "Polaris" },
    { "TONGA*|ICELAND*|TOPAZ*|CARRIZO*|FIJI*|STONEY*|VEGAM*",         "Volcanic Islands" },
    { "BONAIRE*|KABINI*|MULLINS*|KAVERI*|HAWAII*",                    "Sea Islands" },
    { "VERDE*|PITCAIRN*|TAHITI*|OLAND*|HAINAN*",                      "Southern Islands" },
    { "ARUBA*|Trinity*|Richland*|BARTS*|TURKS*|CAICOS*|CAYMAN*",      "Northern Islands" },
    { "Blackcomb*|Whistler*|Seymour*|Robson*|Granville*|Thames*",     "Northern Islands" },
    { "CEDAR*|REDWOOD*|JUNIPER*|CYPRESS*|HEMLOCK*|PALM*|Wrestler*|Ontario*|SUMO*|Llano*",
                                                                      "Evergreen" },
    { "Park*|Madison*|Broadway*|Manhattan*",                          "Evergreen" },
    { "RV7*",                                                         "R700" },
    { "R600*|RV6*|RS78*|RS88*",                                       "R600" },
    { "RV51*|R52*|RV53*|RV56*|RV57*|R58*",                            "R500" },
    { "R4[0-9][0-9]*|RV41*|RS6*|RS7*",                                "R400" },
    { "R3[0-9][0-9]*|RV3*|RS4*",                                      "R300" },
    { "R1[0-9][0-9]*|R2[0-9][0-9]*|RV1*|RV2*|RS1*|RS2*|RS3*",         "R100" },
    { NULL, NULL }
};

/* lspci names Intel iGPUs by CPU generation, not by graphics gen, so the split
 * is by driver era: iris/ANV (Broadwell+), crocus (gen6-7.5, Vulkan only via
 * hasvk), i915 gallium (gen3-5, mesa-amber only). */
static const FamilyRow INTEL[] = {
    { "*2nd Generation*|*3rd Gen*|*4th Gen*|Haswell*|Bay Trail*|*Atom Processor Z3*",
                                                                      "crocus" },
    { "8*|4 Series*|Mobile 4*|G3[0-9]*|Q3[0-9]*|Pineview*|*Core Processor Integrated*",
                                                                      "amber" },
    { NULL, NULL }
};

/* intel_pmu_perms -- make the GPU's load readable without root.
 *
 * Intel is the odd vendor out here: NVIDIA reports utilisation through NVML and
 * AMD through ROCm SMI or amdgpu's gpu_busy_percent in sysfs, both readable by
 * anyone, while i915 publishes engine busy-time only as a perf PMU. Opening a
 * PMU event is CPU-wide, so perf_event_open refuses it for an unprivileged
 * process at the upstream kernel default (perf_event_paranoid = 2) and refuses
 * it outright at Ubuntu's default of 4 -- which is why intel_gpu_top asks for
 * root and why btop just says "Failed to initialize PMU" and shows no GPU.
 *
 * The two ways out are lowering that sysctl to 0 for every process on the box,
 * or giving CAP_PERFMON to the handful of programs that read the PMU. This is
 * the second: narrower, and it leaves the machine's perf policy alone. btop
 * grants itself the same capability (modules/btop.c) because it is installed
 * after this module, not before it.
 *
 * Intel only: no other vendor's monitor needs a capability at all. */
static void intel_pmu_perms(void) {
    (void)osr_setcap("cap_perfmon+ep", "intel_gpu_top");
}

/* intel_family -- the Intel table's fall-through is "modern", not "Unknown". */
static const char *intel_family(const char *chip) {
    const char *f = classify(INTEL, chip);
    return strcmp(f, "Unknown") == 0 ? "modern" : f;
}

static int in(const char *fam, const char *const list[]) {
    size_t i;
    for (i = 0; list[i] != NULL; i++) if (strcmp(fam, list[i]) == 0) return 1;
    return 0;
}

int osrm_gpu_drivers(void) {
    static const char *const nv_current[] = {
        "nvidia-open-dkms", "nvidia-utils", "lib32-nvidia-utils", "nvidia-settings",
        "nvidia-prime", "opencl-nvidia", "lib32-opencl-nvidia", "ocl-icd", "nvtop",
        VK, NULL
    };
    static const char *const nv_570[] = {
        "nvidia-570xx-dkms", "nvidia-570xx-utils", "lib32-nvidia-570xx-utils",
        "nvidia-570xx-settings", "opencl-nvidia-570xx", "ocl-icd", "nvtop", VK, NULL
    };
    static const char *const nv_470[] = {
        "nvidia-470xx-dkms", "nvidia-470xx-utils", "lib32-nvidia-470xx-utils",
        "nvidia-470xx-settings", "opencl-nvidia-470xx", "ocl-icd", "nvtop", VK, NULL
    };
    static const char *const nv_390[] = {
        "nvidia-390xx-dkms", "nvidia-390xx-utils", "lib32-nvidia-390xx-utils",
        "nvidia-390xx-settings", "opencl-nvidia-390xx", "ocl-icd", "libvdpau",
        "vdpauinfo", NULL
    };
    static const char *const nv_340[] = {
        "nvidia-340xx-dkms", "nvidia-340xx-utils", "lib32-nvidia-340xx-utils",
        "nvidia-340xx-settings", "opencl-nvidia-340xx", "ocl-icd", "libvdpau",
        "vdpauinfo", NULL
    };
    static const char *const nouveau[] = {
        "mesa", "lib32-mesa", "mesa-utils", "xf86-video-nouveau", "libvdpau-va-gl",
        "libva-utils", NULL
    };
    static const char *const amd_gcn3[] = {
        "mesa", "lib32-mesa", "mesa-utils", "vulkan-radeon", "lib32-vulkan-radeon",
        "xf86-video-amdgpu", "opencl-mesa", "ocl-icd", "libva-utils", "vdpauinfo",
        "libvdpau-va-gl", "nvtop", VK, NULL
    };
    static const char *const amd_gcn12[] = {
        "mesa", "lib32-mesa", "mesa-utils", "vulkan-radeon", "lib32-vulkan-radeon",
        "xf86-video-ati", "xf86-video-amdgpu", "opencl-mesa", "ocl-icd",
        "libva-utils", "vdpauinfo", "libvdpau-va-gl", VK, NULL
    };
    static const char *const amd_terascale[] = {
        "mesa", "lib32-mesa", "mesa-utils", "xf86-video-ati", "libvdpau",
        "lib32-libvdpau", "libvdpau-va-gl", "libva-utils", "vdpauinfo", NULL
    };
    static const char *const amd_r300[] = {
        "mesa", "lib32-mesa", "mesa-utils", "xf86-video-ati", "libvdpau-va-gl",
        "libva-utils", NULL
    };
    static const char *const amd_amber[] = {
        "mesa-amber", "lib32-mesa-amber", "mesa-utils", "xf86-video-ati", NULL
    };
    static const char *const intel_modern[] = {
        "mesa", "lib32-mesa", "mesa-utils", "vulkan-intel", "lib32-vulkan-intel",
        "intel-media-driver", "libva-utils", "intel-gpu-tools", "nvtop",
        "intel-compute-runtime", "ocl-icd", "clinfo", VK, NULL
    };
    static const char *const intel_crocus[] = {
        "mesa", "lib32-mesa", "mesa-utils", "vulkan-intel", "lib32-vulkan-intel",
        "libva-intel-driver", "lib32-libva-intel-driver", "libva-utils",
        "intel-gpu-tools", "nvtop", VK, NULL
    };
    static const char *const intel_amber[] = {
        "mesa-amber", "lib32-mesa-amber", "mesa-utils", "xf86-video-intel",
        "libva-utils", NULL
    };
    static const char *const vmware[] = {
        "open-vm-tools", "mesa", "lib32-mesa", "mesa-utils", "xf86-video-vmware",
        "vulkan-virtio", "lib32-vulkan-virtio", VK, NULL
    };
    static const char *const vbox[] = {
        "virtualbox-guest-utils", "mesa", "lib32-mesa", "mesa-utils",
        "vulkan-swrast", VK, NULL
    };
    static const char *const qemu[] = {
        "mesa", "lib32-mesa", "mesa-utils", "vulkan-virtio", "lib32-vulkan-virtio",
        "vulkan-swrast", VK, NULL
    };
    static const char *const swrast[] = {
        "mesa", "lib32-mesa", "mesa-utils", "vulkan-swrast", VK, NULL
    };
    /* The NVIDIA families the current open-kernel branch still covers: open
     * kernel modules need a GSP, i.e. Turing and newer. */
    static const char *const nv_current_fams[] = {
        "Blackwell", "Ada Lovelace", "Ampere", "Turing", "Unknown", NULL
    };
    static const char *const nv_570_fams[] = { "Volta", "Pascal", "Maxwell", NULL };
    static const char *const amd_gcn3_fams[] = {
        "Navi", "Vega", "Polaris", "Volcanic Islands", "Unknown", NULL
    };
    static const char *const amd_gcn12_fams[] = { "Sea Islands", "Southern Islands", NULL };
    static const char *const amd_tera_fams[] = {
        "Evergreen", "Northern Islands", "R700", "R600", NULL
    };
    static const char *const amd_r300_fams[] = { "R500", "R400", "R300", NULL };

    const char *vendors = env_str("OSR_GPU_VENDOR", "");
    const char *p = vendors;
    int ok = 1;

    if (strcmp(osr_mod_pkg(), "pacman") != 0)
        osr_warnf("gpu-drivers package names are Arch's - untested on %s, add "
                  "pkgmap rows when it breaks", osr_mod_pkg());

    while (*p != '\0') {
        Str vendor, chip, desc;
        const char *start;
        const char *fam;

        while (is_space(*p)) p++;
        start = p;
        while (*p != '\0' && !is_space(*p)) p++;
        if (p == start) continue;

        str_init(&vendor); str_init(&chip); str_init(&desc);
        str_add(&vendor, start, (size_t)(p - start));
        (void)osr_gpu_chip(&chip, str_text(&vendor));

        if (strcmp(str_text(&vendor), "NVIDIA") == 0) {
            fam = classify(NVIDIA, str_text(&chip));
            osr_infof("NVIDIA chip='%s' family=%s",
                      chip.len > 0 ? str_text(&chip) : "unknown", fam);
            if (in(fam, nv_current_fams)) {
                str_addz(&desc, "Installing NVIDIA drivers ("); str_addz(&desc, fam);
                str_addc(&desc, ')');
                ok = osr_pkg_install_step(str_text(&desc), nv_current) && ok;
            } else if (in(fam, nv_570_fams)) {
                /* Dropped by the 580 branch; 570xx is their last driver. */
                str_addz(&desc, "Installing NVIDIA 570xx drivers ("); str_addz(&desc, fam);
                str_addc(&desc, ')');
                ok = osr_pkg_install_step(str_text(&desc), nv_570) && ok;
            } else if (strcmp(fam, "Kepler") == 0) {
                ok = osr_pkg_install_step("Installing NVIDIA 470xx drivers (Kepler)",
                                          nv_470) && ok;
            } else if (strcmp(fam, "Fermi") == 0) {
                /* 390xx predates NVIDIA Vulkan on Fermi -- VDPAU is the accel path. */
                osr_warn("Fermi: no Vulkan support on the 390xx branch");
                ok = osr_pkg_install_step("Installing NVIDIA 390xx drivers (Fermi)",
                                          nv_390) && ok;
            } else if (strcmp(fam, "Tesla") == 0) {
                osr_warn("Tesla: no Vulkan support on the 340xx branch");
                ok = osr_pkg_install_step("Installing NVIDIA 340xx drivers (Tesla)",
                                          nv_340) && ok;
            } else {
                /* Curie and older: no proprietary branch survives, nouveau is it. */
                osr_warnf("%s (%s) has no maintained NVIDIA driver - using nouveau, "
                          "no Vulkan/OpenCL", fam, str_text(&chip));
                str_addz(&desc, "Installing nouveau ("); str_addz(&desc, fam);
                str_addc(&desc, ')');
                ok = osr_pkg_install_step(str_text(&desc), nouveau) && ok;
            }
        } else if (strcmp(str_text(&vendor), "AMD") == 0) {
            fam = classify(AMD, str_text(&chip));
            osr_infof("AMD chip='%s' family=%s",
                      chip.len > 0 ? str_text(&chip) : "unknown", fam);
            if (in(fam, amd_gcn3_fams)) {
                str_addz(&desc, "Installing AMD drivers ("); str_addz(&desc, fam);
                str_addc(&desc, ')');
                ok = osr_pkg_install_step(str_text(&desc), amd_gcn3) && ok;
            } else if (in(fam, amd_gcn12_fams)) {
                /* GCN 1/2 boot on the radeon DDX by default (amdgpu needs
                 * amdgpu.si_support=1 / cik_support=1 + radeon.*_support=0);
                 * RADV works on either KMS driver, so ship both DDX paths. */
                str_addz(&desc, "Installing AMD GCN1/2 drivers ("); str_addz(&desc, fam);
                str_addc(&desc, ')');
                ok = osr_pkg_install_step(str_text(&desc), amd_gcn12) && ok;
            } else if (in(fam, amd_tera_fams)) {
                /* TeraScale: r600 gallium, no Vulkan (RADV is GCN+). */
                osr_warnf("%s is pre-GCN - no Vulkan, OpenCL is unsupported", fam);
                str_addz(&desc, "Installing AMD TeraScale drivers ("); str_addz(&desc, fam);
                str_addc(&desc, ')');
                ok = osr_pkg_install_step(str_text(&desc), amd_terascale) && ok;
            } else if (in(fam, amd_r300_fams)) {
                osr_warnf("%s is pre-GCN - no Vulkan, OpenCL is unsupported", fam);
                str_addz(&desc, "Installing ATI r300 drivers ("); str_addz(&desc, fam);
                str_addc(&desc, ')');
                ok = osr_pkg_install_step(str_text(&desc), amd_r300) && ok;
            } else if (strcmp(fam, "R100") == 0) {
                /* Fixed-function era: dropped from mainline mesa, amber only. */
                osr_warnf("%s predates programmable shaders - mesa-amber, no Vulkan/OpenCL",
                          fam);
                str_addz(&desc, "Installing ATI amber drivers ("); str_addz(&desc, fam);
                str_addc(&desc, ')');
                ok = osr_pkg_install_step(str_text(&desc), amd_amber) && ok;
            }
        } else if (strcmp(str_text(&vendor), "Intel") == 0) {
            fam = intel_family(str_text(&chip));
            osr_infof("Intel chip='%s' family=%s",
                      chip.len > 0 ? str_text(&chip) : "unknown", fam);
            if (strcmp(fam, "modern") == 0) {
                ok = osr_pkg_install_step("Installing Intel drivers", intel_modern) && ok;
                intel_pmu_perms();
            } else if (strcmp(fam, "crocus") == 0) {
                /* gen6-7.5: crocus for GL, hasvk (shipped in vulkan-intel) for
                 * gen7.5 only, i965 VA-API for video. */
                osr_warn("pre-Broadwell Intel: Vulkan is hasvk-only (gen7.5) and partial");
                ok = osr_pkg_install_step("Installing Intel crocus drivers",
                                          intel_crocus) && ok;
                intel_pmu_perms();
            } else {
                osr_warn("gen3-5 Intel is mesa-amber only - no Vulkan");
                ok = osr_pkg_install_step("Installing Intel amber drivers",
                                          intel_amber) && ok;
            }
        } else if (strcmp(str_text(&vendor), "VMware") == 0) {
            ok = osr_pkg_install_step("Installing VMware GPU drivers", vmware) && ok;
        } else if (strcmp(str_text(&vendor), "VirtualBox") == 0) {
            ok = osr_pkg_install_step("Installing VirtualBox GPU drivers", vbox) && ok;
        } else if (strcmp(str_text(&vendor), "QEMU") == 0) {
            /* virtio-gpu with venus/virgl passthrough, or plain software GL. */
            ok = osr_pkg_install_step("Installing QEMU/virtio GPU drivers", qemu) && ok;
        } else if (strcmp(str_text(&vendor), "Microsoft") == 0) {
            /* Hyper-V / WSLg: hyperv_drm is in-kernel, only userspace is needed. */
            ok = osr_pkg_install_step("Installing Hyper-V GPU drivers", swrast) && ok;
        } else if (strcmp(str_text(&vendor), "Cirrus") == 0
                   || strcmp(str_text(&vendor), "Unknown") == 0) {
            osr_warnf("no vendor-specific driver for GPU '%s' - installing software "
                      "rendering only", str_text(&vendor));
            ok = osr_pkg_install_step("Installing software rendering fallback",
                                      swrast) && ok;
        } else {
            osr_warnf("unknown/unsupported GPU vendor '%s' - skipping driver install",
                      str_text(&vendor));
        }
        str_free(&vendor); str_free(&chip); str_free(&desc);
    }
    return ok;
}
