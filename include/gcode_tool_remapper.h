// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <map>
#include <optional>
#include <string>
namespace helix {

class GcodeToolRemapper {
  public:
    // remap: logical tool index -> physical head index.
    //
    // Rewrites all three command families. Every line is transformed from its
    // OWN original text, so a swap (1<->2) does not chain and no line needs to
    // see any other. That is what lets the same rule run file to file.
    //
    // Peak memory is one line, whatever the file's size, and the byte-for-byte
    // contract is the same as apply_to_string(): unmatched lines pass through
    // untouched and a final line with no trailing newline keeps it that way.
    // Returns the number of lines that changed, or nullopt when either file
    // cannot be opened or any write (including the final flush) fails, so a
    // volume that fills mid-write never yields a truncated file reported whole.
    static std::optional<size_t> apply_to_file(const std::string& in_path,
                                               const std::string& out_path,
                                               const std::map<int, int>& remap);

    // The whole-content form of apply_to_file(), for callers that already hold
    // the content. A file being printed goes file to file instead.
    static std::string apply_to_string(const std::string& gcode, const std::map<int, int>& remap);
};

} // namespace helix
