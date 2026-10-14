#include "draz/storage.h"

#include <fstream>
#include <stdexcept>

namespace draz::storage {

std::string escapeField(const std::string& field) {
    std::string out;
    out.reserve(field.size());
    for (char c : field) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string unescapeField(const std::string& field) {
    std::string out;
    out.reserve(field.size());
    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] != '\\' || i + 1 == field.size()) {
            out.push_back(field[i]);
            continue;
        }
        char next = field[++i];
        switch (next) {
            case 't': out.push_back('\t'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            default: out.push_back(next);
        }
    }
    return out;
}

std::string encodeRow(const Row& row) {
    std::string line;
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (i) line.push_back('\t');
        line += escapeField(row[i]);
    }
    return line;
}

Row decodeRow(const std::string& line) {
    Row row;
    std::string field;
    for (char c : line) {
        if (c == '\t') {
            row.push_back(unescapeField(field));
            field.clear();
        } else {
            field.push_back(c);
        }
    }
    row.push_back(unescapeField(field));
    return row;
}

std::vector<Row> readTable(const std::filesystem::path& file) {
    std::vector<Row> rows;
    std::ifstream in(file, std::ios::binary);
    if (!in) return rows;

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        rows.push_back(decodeRow(line));
    }
    return rows;
}

void writeTable(const std::filesystem::path& file, const std::vector<Row>& rows) {
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path());

    std::filesystem::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + temp.string());
        for (const Row& row : rows) out << encodeRow(row) << '\n';
        out.flush();
        if (!out) throw std::runtime_error("cannot write " + temp.string());
    }
    std::filesystem::rename(temp, file);
}

}  // namespace draz::storage
