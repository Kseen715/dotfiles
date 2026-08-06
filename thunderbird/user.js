// thunderbird/user.js — dotfiles-owned Thunderbird prefs (os-rice §5),
// installed into every profile by modules/thunderbird.sh.

// Required for chrome/userChrome.css (the rice's colors) to be read at all.
user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);

// Follow the desktop's dark preference; i3 draws the border, so drop the
// client-side title bar.
user_pref("mail.uidensity", 0);
user_pref("browser.theme.content-theme", 0);
user_pref("browser.theme.toolbar-theme", 0);
user_pref("mail.tabs.drawInTitlebar", false);

// Portal file picker, so attachments see gvfs mounts (modules/xdg.sh).
user_pref("widget.use-xdg-desktop-portal.file-picker", 1);

// Message display: plain text preferred, remote content blocked. Both are
// memory and privacy wins on a small machine.
user_pref("mailnews.display.prefer_plaintext", false);
user_pref("mailnews.message_display.disable_remote_image", true);
