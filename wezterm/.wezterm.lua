-- | If you changing Windows' terminal, the config is stored in 
-- | "%APPDATA%\Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState\settings.json"

-- Pull in the wezterm API
local wezterm = require("wezterm")
local act = wezterm.action
-- local mux = wezterm.mux
-- This will hold the configuration.
local config = wezterm.config_builder()
-- local gpus = wezterm.gui.enumerate_gpus()
-- config.webgpu_preferred_adapter = gpus[1]
-- config.front_end = "WebGpu"

local enable_logging = false

-- General configuration
config.front_end = "OpenGL"
config.max_fps = 75
config.animation_fps = 1
config.cursor_blink_rate = 500
config.term = "xterm-256color" -- Set the terminal type
config.prefer_egl = true -- Use universal rendering backend
-- pwsh's install location depends on how it was installed (MSI/winget ->
-- Program Files, scoop -> ~\scoop\apps\pwsh\current, choco -> often Program
-- Files too but not guaranteed) -- a single hardcoded path breaks for whoever
-- didn't use that particular installer (wezterm then fails to spawn at all:
-- "exited with code 1", no shell, no error dialog explaining why). Probe the
-- real candidates in order and fall back to relying on PATH, then to
-- Windows PowerShell (always present) rather than a dead terminal.
local function get_windows_pwsh_path()
	local candidates = {
		"C:/Program Files/PowerShell/7/pwsh.exe", -- MSI / winget
		wezterm.home_dir .. "/scoop/apps/pwsh/current/pwsh.exe", -- scoop
		"C:/ProgramData/chocolatey/lib/powershell-core/tools/pwsh.exe", -- choco
	}
	for _, path in ipairs(candidates) do
		local f = io.open(path, "rb")
		if f then
			f:close()
			return path
		end
	end
	-- Not at any known install path -- let Windows resolve it via PATH
	-- (covers scoop's shim, a custom install dir, etc).
	return "pwsh.exe"
end

local windows_pwsh_path = { get_windows_pwsh_path(), "-nologo" }
local linux_shell_path = { os.getenv 'SHELL', "--login" }
config.scrollback_lines = 10000
config.check_for_updates = false

-- Appearance
config.default_cursor_style = "BlinkingBlock"
-- Decorations under GNOME: mutter implements NO server-side decorations for
-- Wayland clients, so TITLE draws nothing there (wezterm#6296) and the only
-- chrome available is wezterm's own INTEGRATED_BUTTONS - whose blank tab-bar
-- area does not initiate a window move under mutter either (wezterm#6025).
-- Running under XWayland instead hands decorations back to mutter: real
-- titlebar, real buttons, draggable. Free here - the displays are 1x, and
-- mutter's xwayland-native-scaling covers the scaled case.
-- Drop enable_wayland=false on a compositor whose Wayland CSD behaves (sway,
-- Hyprland); Super+drag moves the window whatever this is set to.
config.enable_wayland = false
config.window_decorations = "TITLE | RESIZE"
-- config.window_decorations = "INTEGRATED_BUTTONS | RESIZE"
-- config.window_decorations = "NONE | RESIZE"
config.initial_cols = 120
config.initial_rows = 30
config.window_background_opacity = 0.9
config.font_size = 12.0
config.cell_width = 0.9
config.line_height = 0.9
config.window_padding = {
	left = 5,
	right = 5,
	top = 5,
	bottom = 5,
}

-- tabs
config.hide_tab_bar_if_only_one_tab = false
config.use_fancy_tab_bar = true
-- config.tab_bar_at_bottom = true
config.show_tab_index_in_tab_bar = false -- format-tab-title draws its own index below
config.tab_max_width = 40

-- Modern tab styling: floating rounded-pill tabs (Zed/Arc-style) with a
-- per-process icon, separated by gaps of the bar's own background rather than
-- touching edge-to-edge. The caps are half-circle glyphs (ple_*_half_circle)
-- whose shape is unambiguous either way round -- unlike a powerline arrow's
-- direction, a left cap only ever bulges left -- which is why this shape was
-- picked over a chevron divider: nothing to get backwards without a live
-- render to check against. Icon keys are pulled from wezterm's own built-in
-- wezterm.nerdfonts table (verified present against the installed
-- JetBrainsMono Nerd Font, not hand-picked codepoints) so they render
-- correctly rather than as tofu boxes.
-- Matches window_frame's titlebar bg below (#181818) and the terminal's own
-- background -- #010203 (near-pure black) was a hard, unrelated void against
-- both of those, most visible as a stark black strip wherever there's no
-- pill (the gaps, and all the empty bar past the last tab).
local TAB_BAR_BG = "#181818"
local TAB_ACCENT = "#7aa2f7"
local TAB_ACTIVE_BG = "#333a56"
local TAB_ACTIVE_FG = "#ffffff"
local TAB_INACTIVE_BG = "#242631"
local TAB_INACTIVE_FG = "#8b8fa3"
local CAP_LEFT = wezterm.nerdfonts.ple_left_half_circle_thick
local CAP_RIGHT = wezterm.nerdfonts.ple_right_half_circle_thick

local TAB_ICONS = {
	pwsh = wezterm.nerdfonts.seti_powershell,
	powershell = wezterm.nerdfonts.seti_powershell,
	cmd = wezterm.nerdfonts.md_console,
	bash = wezterm.nerdfonts.cod_terminal_bash,
	sh = wezterm.nerdfonts.cod_terminal_bash,
	zsh = wezterm.nerdfonts.seti_shell,
	nvim = wezterm.nerdfonts.custom_vim,
	vim = wezterm.nerdfonts.custom_vim,
	git = wezterm.nerdfonts.dev_git,
	node = wezterm.nerdfonts.dev_nodejs_small,
	python = wezterm.nerdfonts.md_language_python,
	python3 = wezterm.nerdfonts.md_language_python,
	docker = wezterm.nerdfonts.dev_docker,
}
local TAB_ICON_DEFAULT = wezterm.nerdfonts.fa_terminal

local function tab_icon(tab)
	local process = tab.active_pane and tab.active_pane.foreground_process_name or ""
	-- Windows hands back a full "...\pwsh.exe" path; take the basename and
	-- drop the extension so it matches the lookup table either way.
	local name = process:match("([^\\/]+)$") or process
	name = name:gsub("%.exe$", ""):lower()
	return TAB_ICONS[name] or TAB_ICON_DEFAULT
end

wezterm.on("format-tab-title", function(tab, tabs, panes, tab_config, hover, max_width)
	local active = tab.is_active
	local bg = active and TAB_ACTIVE_BG or TAB_INACTIVE_BG
	local fg = active and TAB_ACTIVE_FG or TAB_INACTIVE_FG
	local icon_fg = active and TAB_ACCENT or fg

	local title = (tab.tab_title and #tab.tab_title > 0) and tab.tab_title or tab.active_pane.title
	local budget = math.max(max_width - 8, 6)
	if #title > budget then
		title = title:sub(1, budget - 1) .. "…"
	end

	return {
		-- gap before the pill
		{ Background = { Color = TAB_BAR_BG } },
		{ Text = " " },
		-- left cap: bulges into the bar's background, flat side meets the pill
		{ Foreground = { Color = bg } },
		{ Text = CAP_LEFT },
		-- pill body
		{ Background = { Color = bg } },
		{ Foreground = { Color = icon_fg } },
		{ Text = tab_icon(tab) .. " " },
		{ Foreground = { Color = fg } },
		{ Text = (tab.tab_index + 1) .. ": " .. title },
		-- right cap: mirrors the left, back out to the bar's background
		{ Background = { Color = TAB_BAR_BG } },
		{ Foreground = { Color = bg } },
		{ Text = CAP_RIGHT },
	}
end)

-- config.inactive_pane_hsb = {
-- 	saturation = 0.0,
-- 	brightness = 1.0,
-- }


-- Font configuration with fallback
local font_list = {
	"JetBrainsMonoNL Nerd Font Mono",
	"JetBrainsMonoNL NFM",
    "JetBrains Mono Regular",
	"FiraCode Nerd Font Mono",
    "Cascadia Code",
    "Consolas",
    "Courier New",
}

-- Try to use fonts in order of preference
local installed_fonts = {}
local function is_font_installed_windows_pwsh()
    -- Only run the check once and cache the results
    if next(installed_fonts) == nil then
        -- Use PowerShell to get the list of installed fonts
        local success, stdout, stderr = wezterm.run_child_process({
            "powershell.exe",
			"-NoProfile",
			"-NonInteractive",
			"-ExecutionPolicy", "Bypass",
            "-Command",
            "[System.Reflection.Assembly]::LoadWithPartialName('System.Drawing'); " ..
            "(New-Object System.Drawing.Text.InstalledFontCollection).Families | ForEach-Object { $_.Name }"
        })
        if success then
            for line in stdout:gmatch("[^\r\n]+") do
                -- Convert to lowercase for case-insensitive comparison
                installed_fonts[line:lower()] = true
            end
			if enable_logging then
				wezterm.log_info("Found " .. #installed_fonts .. " installed fonts")
			end
        else
            wezterm.log_error("Failed to get installed fonts: " .. stderr)
            -- If we can't check, assume all fonts are available
            for _, font in ipairs(font_list) do
                installed_fonts[font:lower()] = true
            end
        end
    end
    return installed_fonts
end

local function is_font_installed_linux()
    -- Only run the check once and cache the results
    if next(installed_fonts) == nil then
        -- Use fc-list to get the list of installed fonts
        local success, stdout, stderr = wezterm.run_child_process({
            "fc-list", ":", "family"
        })
        if success then
            for line in stdout:gmatch("[^\r\n]+") do
                -- fc-list can return multiple font names per line, separated by commas
                for font_name in line:gmatch("([^,]+)") do
                    -- Trim whitespace and convert to lowercase for case-insensitive comparison
                    local trimmed = font_name:match("^%s*(.-)%s*$"):lower()
                    if trimmed ~= "" then
                        installed_fonts[trimmed] = true
                    end
                end
            end
        else
            wezterm.log_error("Failed to get installed fonts: " .. stderr)
            -- If we can't check, assume all fonts are available
            for _, font in ipairs(font_list) do
                installed_fonts[font:lower()] = true
            end
        end
    end
    return installed_fonts
end

-- Detect current platform
local function get_platform()
    -- WezTerm provides a target_triple string that identifies the platform
    local target = wezterm.target_triple
	local platform = ""
    if target:find("windows") then
		platform = "windows"
    elseif target:find("linux") then
		platform = "linux"
    elseif target:find("darwin") then
		platform = "macos"
    else
		platform = "unknown"
    end
	if enable_logging then
		wezterm.log_info("Detected platform: " .. platform)
	end
	return platform
end

-- Use correct font detection function based on platform
local function get_installed_fonts()
    local platform = get_platform()
	local fonts = {}
    if platform == "windows" then
        fonts = is_font_installed_windows_pwsh()
    elseif platform == "linux" then
        fonts = is_font_installed_linux()
    elseif platform == "macos" then
        -- You could implement a macOS version if needed
        fonts = is_font_installed_linux() -- MacOS can use fc-list too
    else
        -- Fallback for unknown platforms
        wezterm.log_warning("Unknown platform, assuming all fonts are available")
        for _, font in ipairs(font_list) do
            fonts[font:lower()] = true
        end
    end
	local fonts_count = 0
	for _ in pairs(fonts) do
		fonts_count = fonts_count + 1
	end
	if enable_logging then
		wezterm.log_info("Found " .. fonts_count .. " installed fonts")
	end
	return fonts
end

-- Try to use fonts in order of preference
local function get_font()
    local fonts = get_installed_fonts()
	-- log fonts found
	-- for k, v in pairs(fonts) do
	-- 	wezterm.log_info("Font found: " .. k)
	-- end

    for _, font_name in ipairs(font_list) do
        -- Check if the font family is installed
        local font_family = font_name:match("^([^%s]+)") or font_name
        if fonts[font_name:lower()] or fonts[font_family:lower()] then
			if enable_logging then
				wezterm.log_info("Font found in system: " .. font_name)
			end
            -- Font is available, try to load it safely
            local success, font = pcall(function()
                return wezterm.font(font_name)
            end)
            if success then
				if enable_logging then
					wezterm.log_info("Using font: " .. font_name)
				end
                return font
            end
        else
			if enable_logging then
				wezterm.log_info("Font not available: " .. font_name)
			end
        end
    end
    -- If none of the preferred fonts are available or loadable, use the system default
	if enable_logging then
		wezterm.log_info("No preferred fonts found or loadable, using system default")
	end
    return wezterm.font({})
end

config.font = get_font()


local platform = get_platform()

if platform == "windows" then
	config.default_prog = windows_pwsh_path
elseif platform == "linux" then
	config.default_prog = linux_shell_path
end



-- color scheme toggling
-- wezterm.on("toggle-colorscheme", function(window, pane)
-- 	local overrides = window:get_config_overrides() or {}
-- 	if overrides.color_scheme == "Zenburn" then
-- 		overrides.color_scheme = "Cloud (terminal.sexy)"
-- 	else
-- 		overrides.color_scheme = "Zenburn"
-- 	end
-- 	window:set_config_overrides(overrides)
-- end)

-- keymaps
config.keys = {
	-- {
	-- 	key = "E",
	-- 	mods = "CTRL|SHIFT|ALT",
	-- 	action = wezterm.action.EmitEvent("toggle-colorscheme"),
	-- },
	{
		key = "h",
		mods = "CTRL|SHIFT|ALT",
		action = wezterm.action.SplitPane({
			direction = "Right",
			size = { Percent = 50 },
		}),
	},
	{
		key = "v",
		mods = "CTRL|SHIFT|ALT",
		action = wezterm.action.SplitPane({
			direction = "Down",
			size = { Percent = 50 },
		}),
	},
	{
		key = "U",
		mods = "CTRL|SHIFT",
		action = act.AdjustPaneSize({ "Left", 5 }),
	},
	{
		key = "I",
		mods = "CTRL|SHIFT",
		action = act.AdjustPaneSize({ "Down", 5 }),
	},
	{
		key = "O",
		mods = "CTRL|SHIFT",
		action = act.AdjustPaneSize({ "Up", 5 }),
	},
	{
		key = "P",
		mods = "CTRL|SHIFT",
		action = act.AdjustPaneSize({ "Right", 5 }),
	},
	{
		key = "Q",
		mods = "SHIFT|ALT",
		-- Close current pane directly without sending exit command
		action = act.CloseCurrentPane { confirm = false },
	},
	{ key = "9", mods = "CTRL", action = act.PaneSelect },
	{ key = "L", mods = "CTRL", action = act.ShowDebugOverlay },
	{
		key = "O",
		mods = "CTRL|ALT",
		-- toggling opacity
		action = wezterm.action_callback(function(window, _)
			local overrides = window:get_config_overrides() or {}
			if overrides.window_background_opacity == 1.0 then
				overrides.window_background_opacity = 0.9
			else
				overrides.window_background_opacity = 1.0
			end
			window:set_config_overrides(overrides)
		end),
	},
}

-- Palette: the rice-owned theme layer os-rice installs as
-- ~/.config/wezterm/colors/osr-rice.toml (swapped on rice switch). When it is
-- absent -- an un-riced host, or Windows -- fall back to a built-in scheme
-- instead of letting wezterm complain about an unknown scheme name.
--
-- The path is spelled out from home_dir, and color_scheme_dirs is set to match,
-- because wezterm.config_dir is the dir holding THIS file: for ~/.wezterm.lua
-- that is $HOME, so both the check and wezterm's own scheme search would look in
-- $HOME/colors and never find the palette.
local osr_colors_dir = wezterm.home_dir .. "/.config/wezterm/colors"
local osr_scheme = io.open(osr_colors_dir .. "/osr-rice.toml")
if osr_scheme then
	osr_scheme:close()
	config.color_scheme_dirs = { osr_colors_dir }
	config.color_scheme = "osr-rice"
else
	config.color_scheme = "Apple System Colors"
end
-- config.color_scheme = "Astrodark (Gogh)" -- too bright
-- config.color_scheme = "Breath Silverfox (Gogh)"
-- config.color_scheme = "Campbell (Gogh)" -- classy pwsh
-- config.color_scheme = "darkmoss (base16)" -- candy warm
-- config.color_scheme = "GruvboxDarkHard"

-- Window chrome only: background/cursor/selection come from the palette above,
-- so a rice switch actually changes them. active_tab/inactive_tab here are the
-- base colors wezterm falls back to outside what format-tab-title explicitly
-- paints (padding, hover edges) -- kept in sync with TAB_ACTIVE_*/TAB_INACTIVE_*
-- above so there's no color seam between the two.
config.colors = {
	tab_bar = {
		background = "#181818",
		active_tab = {
			bg_color = "#333a56",
			fg_color = "#ffffff",
			intensity = "Bold",
			underline = "None",
			italic = false,
			strikethrough = false,
		},
		inactive_tab = {
			bg_color = "#242631",
			fg_color = "#8b8fa3",
			intensity = "Normal",
			underline = "None",
			italic = false,
			strikethrough = false,
		},
		inactive_tab_hover = {
			bg_color = "#2c2f3d",
			fg_color = "#7aa2f7",
			italic = false,
		},
		new_tab = {
			bg_color = "#181818",
			fg_color = "#7aa2f7",
			italic = false,
		},
		new_tab_hover = {
			bg_color = "#333a56",
			fg_color = "#7aa2f7",
			italic = true,
		},
	},
}

config.window_frame = {
	font = config.font,
	active_titlebar_bg = "#181818",
	inactive_titlebar_bg = "#181818",
}



-- wezterm.on("gui-startup", function(cmd)
-- 	local args = {}
-- 	if cmd then
-- 		args = cmd.args
-- 	end
--
-- 	local tab, pane, window = mux.spawn_window(cmd or {})
-- 	-- window:gui_window():maximize()
-- 	-- window:gui_window():set_position(0, 0)
-- end)

return config
