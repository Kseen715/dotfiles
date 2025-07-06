info "Downloading dotfiles from Kseen715..."
DOTFILES_KSEEN715_REPO="$TMP_FOLDER/dotfiles_Kseen715"
trace rm -rf $DOTFILES_KSEEN715_REPO
trace git clone https://github.com/Kseen715/dotfiles $DOTFILES_KSEEN715_REPO --depth 1