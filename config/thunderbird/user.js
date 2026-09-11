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

// --- Exchange / Office 365 ---------------------------------------------------
// Thunderbird 140+ speaks EWS natively — no add-on. Account Setup then offers
// "Exchange" beside IMAP/POP, and Office 365 signs in through OAuth2 (the
// built-in client ID), so a work account needs nothing but the address.
//
// Both prefs default to true on current builds; they are pinned here because
// mail.ews.enabled is what actually makes the Exchange option appear, and an
// on-prem server is only found at all through the AutoDiscover probe.
user_pref("mail.ews.enabled", true);
user_pref("mailnews.auto_config.fetchFromExchange.enabled", true);

// On-prem Exchange behind Kerberos/NTLM SSO: uncomment and list the domain, or
// the login prompt loops. Left off by default — it hands credentials to every
// host in the list.
// user_pref("network.negotiate-auth.trusted-uris", "mail.example.com");
