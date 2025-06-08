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
local windows_pwsh_path = { "C:/Program Files/PowerShell/7/pwsh.exe", "-nologo" }
local linux_shell_path = { wezterm.getenv("SHELL") }
config.scrollback_lines = 10000
config.check_for_updates = false

-- Appearance
config.default_cursor_style = "BlinkingBlock"
config.window_decorations = "INTEGRATED_BUTTONS | RESIZE"
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

config.color_scheme = "Apple System Colors"
-- config.color_scheme = "Astrodark (Gogh)" -- too bright
-- config.color_scheme = "Breath Silverfox (Gogh)"
-- config.color_scheme = "Campbell (Gogh)" -- classy pwsh
-- config.color_scheme = "darkmoss (base16)" -- candy warm
-- config.color_scheme = "GruvboxDarkHard"

config.colors = {
	background = "#000000",
	cursor_border = "#555555",
	cursor_fg = "None",
	cursor_bg = "#777777",
	-- selection_fg = '#281733',

	tab_bar = {
		background = "#010203",
		active_tab = {
			bg_color = "#010203",
			fg_color = "#fdfeff",
			intensity = "Normal",
			-- undderlined
			underline = "None",
			italic = false,
			strikethrough = false,
		},
		inactive_tab = {
			bg_color = "#010203",
			fg_color = "#a8a9aa",
			intensity = "Normal",
			underline = "None",
			italic = false,
			strikethrough = false,
		},
		new_tab = {
			bg_color = "#010203",
			fg_color = "#fdfeff",
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
