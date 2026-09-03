/* test/unit_c/detect_test.c -- what lib/detect.c must find out about a box.
 *
 * Detection is the input to everything else: the pkgmap facet ladder, every
 * `require:` predicate, the service verbs, the GPU driver stack, the swap
 * plan. A field that comes back wrong does not fail here -- it fails four
 * modules later, as the wrong package on the wrong distro.
 *
 * Hermetic, and unusually completely so: every probe is either a PATH command
 * (lscpu, lspci, dmidecode, systemd-detect-virt) or a filesystem path the unit
 * lets the caller override ($OSR_MEMINFO, $OSR_DRM, $OSR_ACCEL, $OSR_OS_RELEASE).
 * With both pinned, the whole detector is a pure function from its inputs, and
 * every scenario below is a fixture rather than a fact about the machine
 * running the suite.
 *
 * WHAT IS ASSERTED, AND WHY IT IS THE FALLBACKS
 *
 * The happy paths are dull -- lscpu says x86_64, the answer is x86_64. What is
 * worth a test is every branch taken when a probe is ABSENT or LYING, because
 * those are the boxes people actually report bugs from: a VM whose lscpu has
 * no topology, an unprivileged dmidecode that prints a banner and exits 1, a
 * container with no lspci, an Intel iGPU whose lspci name is a codename with
 * the real name in brackets. Each of those has a scenario here.
 *
 * Replaces test/unit/detect_c_parity.sh and hw_detect.sh, which drove
 * lib/detect.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

/* facts -- the assignments `osr detect <what>` printed, as one blob. */
static const char *facts(const char *what) {
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "detect", what, (const char *)NULL);
    return osr_sb_capture(&sb);
}

/* field -- the value of one OSR_* assignment in the last detection. */
static const char *field(const char *name) {
    static HStr held;
    static int ready = 0;
    HStr needle;
    const char *at;
    const char *end;

    if (!ready) { hs_init(&held); ready = 1; }
    hs_reset(&held);
    hs_init(&needle);
    hs_addc(&needle, '\n');
    hs_add(&needle, name);
    hs_add(&needle, "='");

    {
        const char *hay = osr_sb_capture(&sb);
        /* The first assignment has no leading newline of its own. */
        if (strncmp(hay, hs_text(&needle) + 1, strlen(hs_text(&needle)) - 1) == 0) {
            at = hay + strlen(hs_text(&needle)) - 1;
        } else {
            at = strstr(hay, hs_text(&needle));
            if (at != NULL) at += strlen(hs_text(&needle));
        }
    }
    hs_free(&needle);
    if (at == NULL) return "(unset)";
    end = strchr(at, '\'');
    if (end == NULL) return "(unterminated)";
    {
        const char *p;
        for (p = at; p < end; p++) hs_addc(&held, *p);
    }
    return hs_text(&held);
}

static void is(const char *name, const char *expected, const char *label) {
    osr_assert_eq(expected, field(name), label);
}
static void contains(const char *name, const char *needle, const char *label) {
    osr_assert_true(strstr(field(name), needle) != NULL, label);
}

/* tool -- a probe that prints `out` and exits `code`. */
static void tool(const char *name, const char *out, int code) {
    HStr body;
    hs_init(&body);
    if (out != NULL && *out != '\0') {
        hs_add(&body, "cat <<'PROBE_EOF'\n");
        hs_add(&body, out);
        hs_add(&body, "PROBE_EOF\n");
    }
    hs_add(&body, "exit ");
    hs_addn(&body, (long)code);
    hs_addc(&body, '\n');
    osr_sb_stub_body(&sb, name, hs_text(&body));
    hs_free(&body);
}

/* point -- an OSR_* path knob at a directory or file under the sandbox. */
static void point(const char *var, const char *rel) {
    HStr p;
    hs_init(&p);
    hs_path(&p, hs_text(&sb.root), rel);
    osr_sb_env(&sb, var, hs_text(&p));
    hs_free(&p);
}

int main(void) {
    osr_sb_init(&sb);

    /* ================================================================
     * 1. CPU
     * ================================================================ */
    tool("lscpu",
        "Architecture:            x86_64\n"
        "Vendor ID:               GenuineIntel\n"
        "Model name:              Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz\n"
        "CPU(s):                  8\n"
        "Socket(s):               1\n"
        "Core(s) per socket:      8\n"
        "Thread(s) per core:      1\n", 0);
    facts("cpu");
    is("OSR_CPU_ARCH", "x86_64", "cpu: the architecture is read from lscpu");
    contains("OSR_CPU_MODEL", "i7-9700K", "cpu: the model name is read");
    is("OSR_CPU_CORES", "8", "cpu: cores come from the socket topology");
    is("OSR_CPU_THREADS", "8", "cpu: threads come from the CPU count");

    /* Plenty of VMs and most ARM boards report no socket/core topology at all.
     * Without a fallback the core count comes back empty, and the module that
     * sizes a build job by it then runs `make -j`. */
    tool("lscpu",
        "Architecture: x86_64\n"
        "CPU(s):                  4\n", 0);
    facts("cpu");
    is("OSR_CPU_CORES", "4",
       "cpu: with no topology reported, cores fall back to the thread count");

    /* An ARM SoC: lscpu's model name is the CORE, and reporting "Cortex-A72"
     * to somebody asking what CPU a Pi has is the wrong answer. The device
     * tree names the chip. */
    tool("lscpu",
        "Architecture:            aarch64\n"
        "Vendor ID:               ARM\n"
        "Model name:              Cortex-A72\n"
        "CPU(s):                  4\n", 0);
    osr_sb_write(&sb, "dt/compatible", "brcm,bcm2711", 0644);
    point("OSR_DEVICETREE", "dt");
    facts("cpu");
    is("OSR_CPU_MODEL", "BCM2711 Cortex-A72",
       "cpu: the device-tree SoC is prefixed to the ARM core name");
    osr_sb_env(&sb, "OSR_DEVICETREE", "");

    /* An ARM SoC: lscpu names the CPU CORE, which is not what "what CPU is
     * this" means -- the chip is only in the device tree. And a heterogeneous
     * chip has one lscpu model name for two kinds of core, so the mix comes
     * from the per-processor parts in /proc/cpuinfo. */
    point("OSR_DEVICETREE", "dt");
    osr_sb_write(&sb, "dt/compatible", "brcm,bcm2711", 0644);
    tool("lscpu",
        "Architecture:            aarch64\n"
        "Vendor ID:               ARM\n"
        "Model name:              Cortex-A72\n"
        "CPU(s):                  4\n", 0);
    facts("cpu");
    is("OSR_CPU_MODEL", "BCM2711 Cortex-A72",
       "cpu: the SoC from the device tree names the chip, the core follows it");

    /* An ARM core name carries no clock the way an Intel brand string does,
     * so the speed has to come off lscpu's own max. */
    tool("lscpu",
        "Architecture:            aarch64\n"
        "Model name:              Cortex-A72\n"
        "CPU(s):                  4\n"
        "CPU max MHz:             1800.0000\n", 0);
    facts("cpu");
    is("OSR_CPU_MODEL", "BCM2711 Cortex-A72 @ 1.80GHz",
       "cpu: with no clock in the model name the lscpu max is appended");

    /* The Intel brand string already ends in "@ 3.90GHz" -- appending lscpu's
     * turbo max on top of it would print the speed twice. */
    tool("lscpu",
        "Architecture:            x86_64\n"
        "Model name:              Intel(R) Core(TM) i3-7100 CPU @ 3.90GHz\n"
        "CPU(s):                  4\n"
        "CPU max MHz:             3900.0000\n", 0);
    facts("cpu");
    is("OSR_CPU_MODEL", "BCM2711 Intel(R) Core(TM) i3-7100 CPU @ 3.90GHz",
       "cpu: a model name that already states its clock is left alone");

    osr_sb_write(&sb, "cpuinfo",
        "processor\t: 0\nCPU implementer\t: 0x41\nCPU part\t: 0xd05\n"
        "processor\t: 1\nCPU implementer\t: 0x41\nCPU part\t: 0xd05\n"
        "processor\t: 2\nCPU implementer\t: 0x41\nCPU part\t: 0xd0b\n"
        "processor\t: 3\nCPU implementer\t: 0x41\nCPU part\t: 0xd0b\n", 0644);
    point("OSR_CPUINFO", "cpuinfo");
    tool("lscpu",
        "Architecture:            aarch64\n"
        "Model name:              Cortex-A55\n"
        "CPU(s):                  4\n", 0);
    facts("cpu");
    is("OSR_CPU_MODEL", "BCM2711 2x Cortex-A55 + 2x Cortex-A76",
       "cpu: a big.LITTLE chip reports both core types with their counts");

    /* x86: no CPU part lines at all, and no device tree. Nothing may touch
     * the model name lscpu gave. */
    osr_sb_write(&sb, "cpuinfo", "processor\t: 0\nmodel name\t: Intel(R) Core(TM) i7-9700K\n", 0644);
    osr_sb_env(&sb, "OSR_DEVICETREE", "");
    tool("lscpu",
        "Architecture:            x86_64\n"
        "Model name:              Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz\n"
        "CPU(s):                  8\n", 0);
    facts("cpu");
    contains("OSR_CPU_MODEL", "i7-9700K",
       "cpu: with no ARM parts and no device tree the lscpu model stands");
    osr_sb_env(&sb, "OSR_CPUINFO", "");

    /* ================================================================
     * 2. GPU
     * ================================================================ */
    tool("lspci",
        "00:02.0 \"VGA compatible controller\" \"Intel Corporation\" "
        "\"UHD Graphics 630\" -r02 \"Dell\" \"Device 0704\"\n"
        "01:00.0 \"3D controller\" \"NVIDIA Corporation\" \"GeForce RTX 3080\" "
        "-ra1 \"Foo\" \"Device 1\"\n", 0);
    facts("gpu");
    is("OSR_GPU_COUNT", "2", "gpu: both devices are counted");
    contains("OSR_GPU_VENDOR", "Intel", "gpu: the Intel vendor is normalised");
    contains("OSR_GPU_VENDOR", "NVIDIA", "gpu: the NVIDIA vendor is normalised");
    is("OSR_GPU_MODEL", "Intel UHD Graphics 630, NVIDIA GeForce RTX 3080",
       "gpu: the models are joined with the vendor prefixed exactly once");

    /* How lspci really names an Intel iGPU: the engineering codename first,
     * the name a user would recognise in brackets. Reporting "Kaby Lake-S GT2"
     * to somebody asking what graphics they have is not an answer. */
    tool("lspci",
        "00:02.0 \"VGA compatible controller\" \"Intel Corporation\" "
        "\"Kaby Lake-S GT2 [HD Graphics 630]\" -r04 -p00 \"Gigabyte\" "
        "\"Device d000\"\n", 0);
    facts("gpu");
    is("OSR_GPU_MODEL", "Intel HD Graphics 630",
       "gpu: the bracketed marketing name is preferred over the codename");

    /* No lspci at all -- a container, a minimal Alpine. sysfs still knows the
     * PCI vendor id, which is enough to pick a driver stack. */
    tool("lspci", "", 1);
    osr_sb_write(&sb, "drm/card0/device/vendor", "0x1002\n", 0644);
    point("OSR_DRM", "drm");
    facts("gpu");
    is("OSR_GPU_VENDOR", "AMD",
       "gpu: with no lspci, the vendor comes from the sysfs DRM PCI id");
    is("OSR_GPU_COUNT", "1", "gpu: and the device is still counted");
    osr_sb_env(&sb, "OSR_DRM", "");

    /* An SoC GPU is on no PCI bus at all, so it has neither an lspci line nor
     * a sysfs vendor id -- only a device-tree name. A Pi 4 exposes it as two
     * DRM nodes, display and render core, which are one GPU. */
    tool("lspci", "", 1);
    osr_sb_write(&sb, "soc/card0/device/of_node/compatible", "brcm,2711-v3d", 0644);
    osr_sb_write(&sb, "soc/card1/device/of_node/compatible", "brcm,bcm2711-vc5", 0644);
    point("OSR_DRM", "soc");
    facts("gpu");
    is("OSR_GPU_VENDOR", "Broadcom",
       "gpu: with no PCI device, the vendor comes from the device tree");
    is("OSR_GPU_MODEL", "Broadcom bcm2711-vc5",
       "gpu: the display node names the GPU, not the render core");
    is("OSR_GPU_COUNT", "1", "gpu: and both nodes count as the one SoC GPU");
    /* An SoC GPU: no PCI at all, so no vendor id either -- the DRM node is
     * named by its device-tree compatible. The Pi 4 exposes its display and
     * render cores as two nodes of one GPU. */
    osr_sb_write(&sb, "drm2/card0/device/of_node/compatible", "brcm,2711-v3d\n", 0644);
    osr_sb_write(&sb, "drm2/card1/device/of_node/compatible", "brcm,bcm2711-vc5\n", 0644);
    point("OSR_DRM", "drm2");
    facts("gpu");
    is("OSR_GPU_VENDOR", "Broadcom",
       "gpu: an SoC GPU is named from its device-tree compatible");
    is("OSR_GPU_MODEL", "Broadcom bcm2711-vc5", "gpu: the display node names it");
    is("OSR_GPU_COUNT", "1", "gpu: its two DRM nodes are one GPU");
    osr_sb_env(&sb, "OSR_DRM", "");

    /* ================================================================
     * 3. RAM
     *
     * The size comes from /proc/meminfo, which every box has. Everything
     * else -- type, speed, how many sticks, how many channels -- comes from
     * DMI, which needs root. So the interesting scenario is the unprivileged
     * one, because that is what a normal `osr install` sees.
     * ================================================================ */
    osr_sb_write(&sb, "meminfo", "MemTotal:       32756432 kB\nMemFree: 100 kB\n", 0644);
    point("OSR_MEMINFO", "meminfo");
    tool("dmidecode",
        "Memory Device\n"
        "\tSize: 16384 MB\n"
        "\tLocator: Controller0-ChannelA-DIMM0\n"
        "\tType: DDR4\n"
        "\tSpeed: 3200 MT/s\n"
        "\tConfigured Memory Speed: 3200 MT/s\n"
        "\n"
        "Memory Device\n"
        "\tSize: 16384 MB\n"
        "\tLocator: Controller0-ChannelB-DIMM0\n"
        "\tType: DDR4\n"
        "\tSpeed: 3200 MT/s\n"
        "\n"
        "Memory Device\n"
        "\tSize: No Module Installed\n"
        "\tLocator: Controller0-ChannelC-DIMM0\n"
        "\tType: Unknown\n", 0);
    facts("ram");
    is("OSR_RAM_TOTAL", "31.23GiB",
       "ram: the total keeps its fraction -- 31.23GiB, not 31GiB");
    is("OSR_RAM_TYPE", "DDR4", "ram: the type comes from DMI");
    is("OSR_RAM_SPEED", "3200MT/s", "ram: the speed comes from DMI");
    is("OSR_RAM_STICKS", "2",
       "ram: an empty slot is not counted as a stick");
    is("OSR_RAM_CHANNELS", "2",
       "ram: channels are counted from populated slots only");

    /* dmidecode without root prints a banner on STDOUT and then exits 1. A
     * parser that trusted the exit status would be fine; one that trusted the
     * output would report a machine with one stick of "3.6". */
    tool("dmidecode",
        "# dmidecode 3.6\n"
        "Scanning /dev/mem for entry point.\n", 1);
    facts("ram");
    is("OSR_RAM_TOTAL", "31.23GiB",
       "ram: the size is still detected without DMI -- it comes from meminfo");
    is("OSR_RAM_TYPE", "",
       "ram: the type is empty rather than invented when DMI says nothing");
    is("OSR_RAM_STICKS", "0",
       "ram: an unprivileged dmidecode's banner is not parsed as a stick");
    osr_sb_env(&sb, "OSR_MEMINFO", "");

    /* ================================================================
     * 4. NPU
     * ================================================================ */
    osr_sb_write(&sb, "accel/accel0/device/vendor", "0x8086\n", 0644);
    point("OSR_ACCEL", "accel");
    facts("npu");
    is("OSR_NPU_VENDOR", "Intel",
       "npu: an Intel VPU is found through the kernel accel subsystem");
    is("OSR_NPU_COUNT", "1", "npu: and counted");

    /* No accel subsystem: an older kernel, or a vendor that never wired one
     * up. lspci still reports the device class. */
    point("OSR_ACCEL", "accel/none");
    tool("lspci",
        "c5:00.0 \"Processing accelerators\" "
        "\"Advanced Micro Devices, Inc. [AMD]\" \"AMD IPU Device\" -r10 "
        "\"AMD\" \"Device 1\"\n", 0);
    facts("npu");
    is("OSR_NPU_VENDOR", "AMD",
       "npu: with no accel subsystem, the processing-accelerator class in "
       "lspci is the fallback");

    tool("lspci", "", 1);
    facts("npu");
    is("OSR_NPU_VENDOR", "",
       "npu: a box with neither reports no NPU rather than guessing");
    osr_sb_env(&sb, "OSR_ACCEL", "");

    /* ================================================================
     * 5. Virtualisation
     *
     * It decides whether a module touches the kernel, swap, or firmware at
     * all -- a container that installs a GPU driver stack has wasted an hour
     * and broken nothing useful.
     * ================================================================ */
    tool("systemd-detect-virt", "vmware\n", 0);
    facts("virt");
    is("OSR_VIRT", "vmware", "virt: systemd-detect-virt is believed when it answers");

    /* On bare metal systemd-detect-virt says "none" and exits 1 -- but so does
     * a box where it is simply confused, and lscpu's hypervisor line catches
     * some of those. */
    tool("systemd-detect-virt", "none\n", 1);
    tool("lscpu", "Hypervisor vendor:      KVM\n", 0);
    facts("virt");
    is("OSR_VIRT", "kvm",
       "virt: a 'none' answer falls back to lscpu's hypervisor line");

    tool("lscpu", "Architecture:  x86_64\n", 0);
    facts("virt");
    is("OSR_VIRT", "none",
       "virt: with neither probe reporting one, the box is bare metal");

    /* ================================================================
     * 6. The distro
     *
     * /etc/os-release is the only portable answer, and the fields that matter
     * are the ones the pkgmap ladder and `require:` are written against.
     * ================================================================ */
    osr_sb_write(&sb, "os-release",
        "NAME=\"Ubuntu\"\n"
        "VERSION=\"24.04.1 LTS (Noble Numbat)\"\n"
        "ID=ubuntu\n"
        "ID_LIKE=debian\n"
        "VERSION_ID=\"24.04\"\n"
        "VERSION_CODENAME=noble\n", 0644);
    point("OSR_OS_RELEASE", "os-release");
    osr_sb_stub_body(&sb, "apt-get", "exit 0\n");
    facts("all");
    is("OSR_DISTRO", "ubuntu", "distro: ID is the distro");
    is("OSR_ID_LIKE", "debian",
       "distro: ID_LIKE is kept -- it is what a derivative is matched on");
    is("OSR_VERSION_ID", "24.04", "distro: the version id is unquoted");
    is("OSR_CODENAME", "noble", "distro: the codename is read");
    is("OSR_PKG", "apt",
       "distro: the package manager is resolved from the distro, and apt-get "
       "being on PATH is what confirms it");

    /* A distro nothing recognises must SAY so rather than silently picking a
     * package manager: installing with the wrong one is worse than stopping. */
    osr_sb_write(&sb, "os-release",
        "NAME=\"Nothing\"\nID=nothing-at-all\nVERSION_ID=\"1\"\n", 0644);
    osr_sb_rm(&sb, "bin/apt-get");
    facts("all");
    is("OSR_DISTRO", "nothing-at-all", "distro: an unknown ID is still reported");
    is("OSR_PKG", "", "distro: and no package manager is invented for it");
    osr_assert_err(&sb, "could not detect a package manager",
       "distro: an undetectable package manager is warned about by name");

    osr_sb_free(&sb);
    return osr_finish();
}
