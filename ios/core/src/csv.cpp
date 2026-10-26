#include "miniworld/csv.h"

#include <cstdlib>

namespace mw {
namespace {

// Splits one record honouring "double quoted, "" escaped"" fields.
bool splitRecord(std::string_view line, std::vector<std::string> &out) {
    out.clear();
    std::string field;
    bool inQuotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field.push_back(c);
            }
            continue;
        }
        if (c == '"') {
            inQuotes = true;
        } else if (c == ',') {
            out.push_back(field);
            field.clear();
        } else if (c != '\r') {
            field.push_back(c);
        }
    }
    if (inQuotes) return false;
    out.push_back(field);
    return true;
}

}  // namespace

bool CsvTable::parse(std::string_view text) {
    columns_.clear();
    rows_.clear();

    // Strip a UTF-8 BOM.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }

    std::vector<std::string> record;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        const std::string_view line = text.substr(0, nl == std::string_view::npos ? text.size() : nl);
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        if (line.empty()) continue;
        if (!splitRecord(line, record)) return false;
        rows_.push_back(record);
    }
    if (rows_.empty()) return false;
    columns_ = rows_.front();
    return true;
}

void CsvTable::useHeader(std::size_t row) {
    if (row < rows_.size()) columns_ = rows_[row];
}

int CsvTable::columnIndex(std::string_view name) const {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i] == name) return static_cast<int>(i);
    }
    return -1;
}

const std::string *CsvTable::cell(std::size_t row, std::size_t column) const {
    if (row >= rows_.size() || column >= rows_[row].size()) return nullptr;
    return &rows_[row][column];
}

const std::string *CsvTable::cell(std::size_t row, std::string_view columnName) const {
    const int c = columnIndex(columnName);
    if (c < 0) return nullptr;
    return cell(row, static_cast<std::size_t>(c));
}

bool CsvTable::idAt(std::size_t row, int &id) const {
    const std::string *p = cell(row, 0);
    if (!p || p->empty()) return false;
    char *end = nullptr;
    const long v = std::strtol(p->c_str(), &end, 10);
    if (end == p->c_str()) return false;
    id = static_cast<int>(v);
    return true;
}

}  // namespace mw
