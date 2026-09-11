/* lib/git.h -- git clone/update plus the oh-my-zsh install, the C port of
 * lib/git.sh.
 *
 * One copy of logic the old per-distro modules pasted (and drifted apart on).
 * Every git operation runs as OSR_USER (§8), because the tree it touches is
 * that account's, and a byte written under root leaves a repo the session can
 * no longer pull.
 *
 * C89 + POSIX.
 */
#ifndef OSR_GIT_H
#define OSR_GIT_H

#include "common.h"

/* The dotfiles repo itself, and where a fetched copy of it lives.
 *
 * Three places need to agree on this: the `osr` launcher's self-bootstrap
 * (piped from curl, no checkout), provision_tree() in osr.c (a released binary
 * standing alone, no tree beside it) and `osr update --src` (a source update
 * with nothing to update yet). They are the same clone -- whichever of them
 * fetched it, the others must find it rather than make a second copy -- so the
 * URL and the destination are stated once, here, rather than three times.
 *
 * $OSR_REPO_URL and $OSR_DEST override, as they do in the launcher.
 */
#define OSR_DOTFILES_REPO_URL "https://github.com/Kseen715/dotfiles.git"

/* osr_dotfiles_dest -- the checkout directory, into `out`. Returns 0 (and
 * leaves out empty) only when the path would not fit.
 *
 * $HOME/.local/share, not $TMPDIR: a tmpdir is swept between boots, so a tree
 * cloned there cost a fresh download of themes/ and rices/ every session and
 * left an offline box with nothing. A home that cannot be resolved at all (a
 * daemon, a bare container) falls back to the tmpdir -- a swept tree beats no
 * tree.
 */
int osr_dotfiles_dest(char *out, unsigned long out_sz);

/* osr_git_repo -- clone <url> into <dir> if it is absent; if it is there and
 * the remote matches, reset a dirty tree then pull; if the remote differs,
 * throw the tree away and clone again. clone_args is a NULL-terminated vector
 * of extra `git clone` flags (--depth 1, ...), or NULL for none. Idempotent
 * (§2). Fatal on a failed clone or pull, which is check_error's contract.
 */
int osr_git_repo(const char *name, const char *url, const char *dir,
                 char *const clone_args[]);

/* osr_zsh_plugin -- clone/update an oh-my-zsh custom plugin, shallow. */
int osr_zsh_plugin(const char *name, const char *url);

/* osr_install_omz -- install oh-my-zsh unattended when it is absent (§7 G5:
 * an installed program, not config -- one install method, never vendored).
 *
 * The presence probe is the FILE, not the directory: ~/.oh-my-zsh can exist
 * while holding no oh-my-zsh at all. osr_zsh_plugin creates custom/plugins/
 * inside it, and on a distro that packages omz system-wide (Armbian ships
 * /etc/oh-my-zsh and exports ZSH=/etc/oh-my-zsh from its stock ~/.zshrc)
 * nothing else ever writes a core there. A directory probe called that stub
 * "already installed", so the core never landed, `source $ZSH/oh-my-zsh.sh`
 * found nothing to read, and the whole plugin list -- highlighting,
 * autosuggestions, autocomplete, and with it the up-arrow history widget --
 * silently did not load.
 */
int osr_install_omz(void);

#endif /* OSR_GIT_H */
