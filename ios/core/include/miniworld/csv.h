// Minimal CSV reader for the game's data tables.
//
// The shipped tables have two header rows: row 0 is the Chinese description, row 1 is the
// English field name. `headerRow` lets the caller pick which one to treat as columns.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mw {

class CsvTable {
public:
    // Parses `text`; returns false on malformed quoting.
    bool parse(std::string_view text);

    // Uses `row` as the header (0 or 1 for the shipped tables) and keeps later rows as data.
    void useHeader(std::size_t row);

    std::size_t rowCount() const { return rows_.size(); }
    std::size_t columnCount() const { return columns_.size(); }

    int columnIndex(std::string_view name) const;
    const std::string *cell(std::size_t row, std::size_t column) const;
    const std::string *cell(std::size_t row, std::string_view columnName) const;

    // First cell of a row, parsed as an integer (returns false when not numeric).
    bool idAt(std::size_t row, int &id) const;

    const std::vector<std::string> &columns() const { return columns_; }
    const std::vector<std::vector<std::string>> &rows() const { return rows_; }

private:
    std::vector<std::string> columns_;
    std::vector<std::vector<std::string>> rows_;
};

}  // namespace mw
