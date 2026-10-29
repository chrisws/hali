// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef SECURITY_AUDIT_H
#define SECURITY_AUDIT_H

#include <string>

namespace sec_audit {

//
// Check if the sandbox has been security audited.
// Returns true if a valid audit file exists with a matching hash.
//
bool is_audited(const std::string &sandbox);

//
// Write the security audit marker file.
// Only Hali can produce a valid file because the hash incorporates
// a compile-time salt that is not exposed to the model or user.
// Returns true on success.
//
bool mark_audited(const std::string &sandbox);

//
// Remove the audit marker (e.g. after sandbox contents change).
//
void clear_audited(const std::string &sandbox);

//
// Return a human-readable status line for TUI display.
//
std::string status_message(const std::string &sandbox);

} // namespace sec_audit

#endif