-- Kinetic: the app's window.
-- Installed into Omarchy's Hyprland toggles by Kinetic's install.sh; uninstall.sh removes it.

-- The Kinetic app (a window of the Omarchy shell): floating, centered.
hl.window_rule({ match = { class = "^org.quickshell$", title = "^Kinetic$" }, float = true, center = true, size = { 1060, 740 } })

