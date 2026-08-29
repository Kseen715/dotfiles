/* modules/zsh.c -- zsh + prompt + layered rc.d config. ONE copy, POSIX,
 * distro-agnostic: the package line goes through pkg_install/pkgmap, everything
 * else is shared (§Module example).
 *
 * starship (prompt) + its Nerd Font + starship.toml theme live in
 * modules/starship.c, so manifest order lists starship before zsh. zsh only
 * wires the prompt in via its rice-owned 90-theme.zsh.
 *
 * fzf backs the up-arrow history picker in zsh/rc.d/10-omz.zsh (ten rows on
 * screen, the whole history behind them -- complist cannot window a list, fzf
 * can). The rc side is guarded on the binary being present, so a machine without
 * it falls back to the plain completion menu rather than breaking the key; it is
 * listed here so that machine does not exist.
 *
 * Port of modules/zsh.sh, kept as the reference at
 * test/ref/zsh_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/build.h"
#include "../lib/cmds.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/git.h"
#include "../lib/migrate.h"

#include <stddef.h>

/* --- the regions the migrations replace, byte for byte -----------------------
 * seed_once/seed_empty deliberately skip a file that is already there, so none
 * of the fixes below would ever reach an existing machine. Each one is either
 * purely additive or an exact match against text os-rice itself shipped;
 * anything the user has edited is reported by migrate_stale instead of
 * rewritten. */

static const char *const MIG_LOCAL_OLD =
    "# --- ssh-agent: reuse an existing agent, or start one -------------------------\n"
    "# NOTE: start_agent never writes $SSH_ENV, so the -f test below is never true and\n"
    "# a fresh agent gets spawned for every shell. Moved verbatim; not fixed here.\n"
    "SSH_ENV=\"$HOME/.ssh/agent-environment\"\n"
    "\n"
    "start_agent() {\n"
    "    eval \"$(ssh-agent -s)\" >/dev/null\n"
    "    # Only add private keys (ignore .pub, config, known_hosts, etc.)\n"
    "    ssh-add ~/.ssh/* 2>/dev/null\n"
    "}\n"
    "\n"
    "if [ -f \"$SSH_ENV\" ]; then\n"
    "    . \"$SSH_ENV\" >/dev/null\n"
    "    kill -0 \"$SSH_AGENT_PID\" 2>/dev/null || start_agent\n"
    "else\n"
    "    start_agent\n"
    "fi\n"
    "\n"
    "# --- nvm ---------------------------------------------------------------------\n"
    "# Sourced last on purpose: nvm prepends its active node dir to PATH and should\n"
    "# win over the PATH edits in 00-env.zsh.\n"
    "export NVM_DIR=\"$HOME/.nvm\"\n"
    "[ -s \"$NVM_DIR/nvm.sh\" ] && \\. \"$NVM_DIR/nvm.sh\"\n"
    "[ -s \"$NVM_DIR/bash_completion\" ] && \\. \"$NVM_DIR/bash_completion\"\n";

static const char *const MIG_BREW_V1 =
    "# Homebrew shell environment (machine-specific), only if installed.\n"
    "if command -v brew >/dev/null 2>&1; then\n"
    "    eval \"$(brew shellenv)\"\n"
    "fi\n";

static const char *const MIG_BREW_V2 =
    "if [ -z \"${HOMEBREW_PREFIX:-}\" ]; then\n"
    "    if [ -x /home/linuxbrew/.linuxbrew/bin/brew ]; then\n"
    "        eval \"$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)\"\n"
    "    elif command -v brew >/dev/null 2>&1; then\n"
    "        eval \"$(brew shellenv)\"\n"
    "    fi\n"
    "fi\n";

static const char *const MIG_BREW_NEW =
    "# Homebrew shell environment (machine-specific), only if installed. Probed by\n"
    "# absolute path, never `command -v brew`: a PATH lookup that MISSES has to stat\n"
    "# every entry, and under WSL interop that is ~25 /mnt/c dirs (44.5 ms measured).\n"
    "# An install outside these three prefixes needs HOMEBREW_PREFIX exported.\n"
    "if [ -z \"${HOMEBREW_PREFIX:-}\" ]; then\n"
    "    for _osr_brew in /home/linuxbrew/.linuxbrew/bin/brew /opt/homebrew/bin/brew /usr/local/bin/brew; do\n"
    "        [ -x \"$_osr_brew\" ] || continue\n"
    "        eval \"$(\"$_osr_brew\" shellenv)\"\n"
    "        break\n"
    "    done\n"
    "    unset _osr_brew\n"
    "fi\n";

static const char *const MIG_TYPESET =
    "# Keep $path unique for good. The guards above only cover this file; anything\n"
    "# that prepends unconditionally later (brew shellenv, /etc/profile) would still\n"
    "# duplicate. Guarded so a POSIX sh sourcing this file still works.\n"
    "[ -n \"${ZSH_VERSION:-}\" ] && typeset -U path PATH\n";

/* rcdir -- ~/.config/osr/zsh/rc.d, the layered rc directory (§5). */
static void rcdir(Str *out) {
    str_reset(out);
    str_addz(out, osr_mod_home());
    str_addz(out, "/.config/osr/zsh/rc.d");
}

static int omz(void *ctx)   { (void)ctx; return osr_install_omz(); }

typedef struct { const char *name; const char *url; } Plugin;
static int plugin(void *ctx) {
    const Plugin *p = (const Plugin *)ctx;
    return osr_zsh_plugin(p->name, p->url);
}

static int build_fzf(void *ctx) { (void)ctx; return osr_build_run("provide_fzf"); }

/* migrate_layers -- the three fixes that have to reach a box installed before
 * the layer that now owns the text. */
static int migrate_layers(void *ctx) {
    Str dir, local, env;

    (void)ctx;
    str_init(&dir); str_init(&local); str_init(&env);
    rcdir(&dir);
    str_addz(&local, str_text(&dir)); str_addz(&local, "/99-local.zsh");
    str_addz(&env,   str_text(&dir)); str_addz(&env,   "/00-env.zsh");

    /* 1. Drop the legacy tool config now owned by 30-tools.zsh. Sourcing nvm.sh
     *    eagerly cost ~360 ms per shell, and start_agent never wrote $SSH_ENV so
     *    every shell leaked an agent. Both now live in 30-tools.zsh -- and
     *    because 99-local loads LAST, leaving this behind would not just be
     *    slow, it would override the new layer entirely. */
    if (!osr_migrate_replace(str_text(&local), "legacy nvm/ssh-agent -> 30-tools.zsh",
                             MIG_LOCAL_OLD, ""))
        (void)osr_migrate_stale(str_text(&local), "NVM_DIR/nvm\\.sh",
                                "an eager nvm.sh source (~360 ms/shell, and it "
                                "overrides 30-tools.zsh)");

    /* 2. Absolute-path brew probe, whichever generation is on disk. A PATH
     *    lookup that MISSES stats every entry, and under WSL interop $PATH
     *    carries ~25 /mnt/c dirs: 44.5 ms per shell with no brew installed. */
    if (!osr_migrate_replace(str_text(&env), "brew probe -> absolute path",
                             MIG_BREW_V1, MIG_BREW_NEW)
        && !osr_migrate_replace(str_text(&env), "brew probe -> absolute path",
                                MIG_BREW_V2, MIG_BREW_NEW))
        (void)osr_migrate_stale(str_text(&env), "command -v brew",
                                "a `command -v brew` PATH probe (44.5 ms/shell under WSL)");

    /* 3. Additive, so it needs no exact match: without it PATH accumulates
     *    duplicates from anything that prepends unconditionally later. */
    (void)osr_migrate_append(str_text(&env), "typeset -U path",
                             "typeset -U path PATH", MIG_TYPESET);

    str_free(&dir); str_free(&local); str_free(&env);
    return 1;
}

/* set_login_shell -- the step body: a box where no mechanism works warns
 * instead of killing an otherwise good run. */
static int set_login_shell(void *ctx) {
    const char *shell = (const char *)ctx;
    if (osr_set_login_shell(osr_mod_user(), shell)) return 1;
    osr_warnf("could not set the login shell - run: chsh -s %s %s",
              shell, osr_mod_user());
    return 1;
}

int osrm_zsh(void) {
    static const char *const pkgs[] = { "zsh", "git", "curl", "lsd", "fzf", NULL };
    static const Plugin plugins[] = {
        { "zsh-autosuggestions",     "https://github.com/zsh-users/zsh-autosuggestions" },
        { "zsh-syntax-highlighting", "https://github.com/zsh-users/zsh-syntax-highlighting" },
        /* Live prediction dropdown (PSReadLine ListView equivalent). Pure zsh,
         * so it needs no pkgmap row. Load order is load-bearing -- see the
         * plugins array in zsh/rc.d/10-omz.zsh. */
        { "zsh-autocomplete",        "https://github.com/marlonrichert/zsh-autocomplete" }
    };
    /* The dotfiles-owned layers, in load order. 30-tools is dotfiles-owned
     * rather than 99-local so a fresh install gets it; every block inside is
     * guarded, so it is a no-op on a machine with neither nvm nor an agent. */
    static const char *const layers[] = {
        "10-omz.zsh", "20-aliases.zsh", "30-tools.zsh", NULL
    };
    Str dir, src, dst, zsh_bin, desc;
    size_t i;
    int ok;

    ok = osr_pkg_install_step("Installing zsh and tools", pkgs);

    /* ...but presence is not sufficiency: the up-arrow widget draws itself with
     * --gutter, which only exists in fzf 0.66+, and an older fzf exits with
     * `unknown option: --gutter` the moment the key is pressed. The pkgmap rows
     * route the releases known to be behind straight to provide_fzf, and this
     * catches the rest: a box that ALREADY had an old distro fzf (which
     * satisfies pkg_install's presence probe and would never be replaced), an
     * EOL release, or an admin-pinned package. */
    if (!osr_fzf_ok())
        ok = osr_step("Installing fzf >= " OSR_FZF_MIN " (zsh up-arrow history picker)",
                      build_fzf, NULL) && ok;

    ok = osr_step("Installing oh-my-zsh", omz, NULL) && ok;
    for (i = 0; i < sizeof(plugins) / sizeof(plugins[0]); i++) {
        Str step;
        str_init(&step);
        str_addz(&step, "Installing "); str_addz(&step, plugins[i].name);
        ok = osr_step(str_text(&step), plugin, (void *)&plugins[i]) && ok;
        str_free(&step);
    }

    /* Layered rc.d config (§5): os-rice writes only what it owns. */
    str_init(&dir); str_init(&src); str_init(&dst);
    rcdir(&dir);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/zsh/rc.d/00-env.zsh");
    str_addz(&dst, str_text(&dir));     str_addz(&dst, "/00-env.zsh");
    ok = osr_seed_once(str_text(&src), str_text(&dst)) && ok;
    for (i = 0; layers[i] != NULL; i++) {
        str_reset(&src); str_reset(&dst);
        str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/zsh/rc.d/");
        str_addz(&src, layers[i]);
        str_addz(&dst, str_text(&dir)); str_addc(&dst, '/'); str_addz(&dst, layers[i]);
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }

    /* rice-owned prompt theme, swapped on rice switch (§6). starship.toml is
     * owned by modules/starship.c (G5), not here. */
    str_reset(&dst);
    str_addz(&dst, str_text(&dir)); str_addz(&dst, "/90-theme.zsh");
    (void)osr_install_theme_layer("zsh", "90-theme.zsh", str_text(&dst));

    str_reset(&dst);
    str_addz(&dst, str_text(&dir)); str_addz(&dst, "/99-local.zsh");
    ok = osr_seed_empty(str_text(&dst)) && ok;

    ok = osr_step("Migrating pre-existing zsh layers", migrate_layers, NULL) && ok;

    /* Thin loader: own only a marked block in ~/.zshrc (§5). */
    str_reset(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.zshrc");
    ok = osr_install_zsh_loader(str_text(&dir), str_text(&dst)) && ok;
    /* ...and a marked block in ~/.zshenv, which is the only file early enough to
     * suppress Ubuntu's duplicate global compinit (82 ms). */
    str_reset(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.zshenv");
    ok = osr_install_zsh_zshenv(str_text(&dst)) && ok;

    /* Default login shell -> zsh, only when it isn't already (§2). No package
     * manager does this for us, and chsh is not everywhere, so
     * osr_set_login_shell walks chsh -> usermod -> /etc/passwd and registers zsh
     * in /etc/shells first. */
    str_init(&zsh_bin); str_init(&desc);
    if (!osr_path_lookup("zsh", &zsh_bin) || zsh_bin.len == 0) {
        osr_warn("zsh not on PATH after install - leaving login shell unchanged");
    } else if (!osr_user_shell_is(osr_mod_user(), str_text(&zsh_bin))) {
        ok = osr_step("Setting default shell to zsh", set_login_shell,
                      (void *)str_text(&zsh_bin)) && ok;
    }
    str_free(&zsh_bin); str_free(&desc);

    str_free(&dir); str_free(&src); str_free(&dst);
    return ok;
}
