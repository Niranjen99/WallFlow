#pragma once

#include <optional>
#include <string>

namespace wallflow::win {

// "Start with Windows" for the current user only (no admin rights needed): a value under
// HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run. Windows runs each value's
// command line when the user logs in.

// The command registered under `name`, if any.
std::optional<std::wstring> autostart_command(const std::wstring& name);

// Registers `command` under `name` (enable) or removes it. Returns false on failure.
bool set_autostart(const std::wstring& name, const std::wstring& command, bool enable);

// This program's own path in quotes, ready to be used as an autostart command.
std::wstring own_command_line();

}  // namespace wallflow::win
