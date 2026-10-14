#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace draz::storage {

using Row = std::vector<std::string>;

// Fields are separated by tabs. Backslash, tab and newline inside a field
// are escaped, so names with spaces or odd characters can't break a file.
std::string escapeField(const std::string& field);
std::string unescapeField(const std::string& field);

std::string encodeRow(const Row& row);
Row decodeRow(const std::string& line);

// Reads every row of a table. A missing file is an empty table.
std::vector<Row> readTable(const std::filesystem::path& file);

// Writes to a temporary file first and then renames it over the old one,
// so a crash halfway through never leaves a half-written table.
void writeTable(const std::filesystem::path& file, const std::vector<Row>& rows);

}  // namespace draz::storage
