// ---------------------------------------------------------------------------
// File-driven CLI harness for the multi-symbol matching engine.
// ---------------------------------------------------------------------------
// The program is self-contained: it reads everything from a single data
// directory (default "data", overridable as argv[1]) laid out as
//
//   <data>/symbols.txt   one symbol per line ('#' comments / blanks ignored)
//   <data>/input.csv     one command per line (see parse_line below)
//   <data>/out/          created if absent; per-symbol <symbol>.log written by
//                        the Sinks, plus errors.log for synchronous rejects
//
// A single producer thread (this thread) replays input.csv in file order, so the
// per-symbol output is deterministic. Concurrency across producers is exercised
// by the benchmark, not here.
//
// Command grammar (comma-separated, surrounding whitespace tolerated):
//   NEW,<client_id>,<client_order_id>,<symbol>,<BUY|SELL>,<LIMIT|MARKET|IOC>,<price>,<qty>
//   CANCEL,<client_id>,<client_order_id>,<symbol>
//   MODIFY,<client_id>,<client_order_id>,<symbol>,<price>,<qty>
// Market orders ignore <price> (any value, e.g. 0, is accepted).

#include "engine/Exchange.hpp"
#include "engine/MatchEngine.hpp"
#include "engine/Types.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace Engine;

// Trims leading/trailing ASCII whitespace without allocating.
[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    const auto not_space = [](char c) {
        return c != ' ' && c != '\t' && c != '\r' && c != '\n';
    };
    while (!s.empty() && !not_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && !not_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

// Splits `line` on commas into trimmed fields.
[[nodiscard]] std::vector<std::string_view> split(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t comma = line.find(',', start);
        const std::size_t end = (comma == std::string_view::npos) ? line.size()
                                                                   : comma;
        out.push_back(trim(line.substr(start, end - start)));
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

// Parses a signed 64-bit integer from a trimmed field. Returns nullopt on any
// non-numeric or out-of-range input.
[[nodiscard]] std::optional<std::int64_t> parse_i64(std::string_view s) noexcept {
    std::int64_t value = 0;
    const char* first = s.data();
    const char* last = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc{} || ptr != last) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<Side> parse_side(std::string_view s) noexcept {
    if (s == "BUY") return Side::BUY;
    if (s == "SELL") return Side::SELL;
    return std::nullopt;
}

[[nodiscard]] std::optional<OrderType> parse_new_type(std::string_view s) noexcept {
    if (s == "LIMIT") return OrderType::Limit;
    if (s == "MARKET") return OrderType::Market;
    if (s == "IOC") return OrderType::IOC;
    return std::nullopt;
}

// The outcome of turning one input line into a command.
struct ParsedCommand {
    std::string  symbol;
    OrderRequest request{};
};

// Parses one non-comment, non-blank line. Returns nullopt on a malformed line
// (wrong field count, bad enum, non-numeric field).
[[nodiscard]] std::optional<ParsedCommand> parse_line(std::string_view line) {
    const std::vector<std::string_view> f = split(line);
    if (f.empty()) {
        return std::nullopt;
    }
    const std::string_view action = f[0];

    // Common leading fields for every command kind.
    auto parse_common = [&](std::size_t expected) -> std::optional<ParsedCommand> {
        if (f.size() != expected) {
            return std::nullopt;
        }
        const auto cid = parse_i64(f[1]);
        const auto coid = parse_i64(f[2]);
        if (!cid || !coid || f[3].empty()) {
            return std::nullopt;
        }
        ParsedCommand pc;
        pc.symbol = std::string(f[3]);
        pc.request.clientId = static_cast<uint64_t>(*cid);
        pc.request.clientOrderId = static_cast<uint64_t>(*coid);
        return pc;
    };

    if (action == "NEW") {
        auto pc = parse_common(8);
        if (!pc) return std::nullopt;
        const auto side = parse_side(f[4]);
        const auto type = parse_new_type(f[5]);
        const auto price = parse_i64(f[6]);
        const auto qty = parse_i64(f[7]);
        if (!side || !type || !price || !qty) {
            return std::nullopt;
        }
        pc->request.side = *side;
        pc->request.type = *type;
        pc->request.price = static_cast<uint64_t>(*price);
        pc->request.quantity = static_cast<uint64_t>(*qty);
        return pc;
    }
    if (action == "CANCEL") {
        auto pc = parse_common(4);
        if (!pc) return std::nullopt;
        pc->request.type = OrderType::Cancel;
        return pc;
    }
    if (action == "MODIFY") {
        auto pc = parse_common(6);
        if (!pc) return std::nullopt;
        const auto price = parse_i64(f[4]);
        const auto qty = parse_i64(f[5]);
        if (!price || !qty) {
            return std::nullopt;
        }
        pc->request.type = OrderType::Modify;
        pc->request.price = static_cast<uint64_t>(*price);
        pc->request.quantity = static_cast<uint64_t>(*qty);
        return pc;
    }
    return std::nullopt;  // unknown action keyword
}

// Loads symbols from <path>: one per line, '#' comments and blank lines ignored.
[[nodiscard]] std::vector<std::string> load_symbols(const std::filesystem::path& path) {
    std::vector<std::string> symbols;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const std::string_view s = trim(line);
        if (s.empty() || s.front() == '#') {
            continue;
        }
        symbols.emplace_back(s);
    }
    return symbols;
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path data_dir = (argc > 1) ? argv[1] : "data";
    const std::filesystem::path symbols_path = data_dir / "symbols.txt";
    const std::filesystem::path input_path = data_dir / "input.csv";
    const std::filesystem::path out_dir = data_dir / "out";

    if (!std::filesystem::exists(symbols_path)) {
        std::cerr << "error: missing symbols file: " << symbols_path << '\n';
        return 1;
    }
    if (!std::filesystem::exists(input_path)) {
        std::cerr << "error: missing input file: " << input_path << '\n';
        return 1;
    }

    const std::vector<std::string> symbols = load_symbols(symbols_path);
    if (symbols.empty()) {
        std::cerr << "error: no symbols configured in " << symbols_path << '\n';
        return 1;
    }

    // The Exchange creates out_dir and opens one <symbol>.log per symbol.
    Engine::Exchange exchange(symbols, out_dir.string());
    exchange.start();

    std::ofstream errors(out_dir / "errors.log",
                         std::ios::out | std::ios::trunc);

    std::ifstream input(input_path);
    std::string line;
    std::size_t line_no = 0;
    std::size_t accepted = 0;
    std::size_t rejected = 0;
    while (std::getline(input, line)) {
        ++line_no;
        const std::string_view view = trim(line);
        if (view.empty() || view.front() == '#') {
            continue;  // blank or comment line
        }
        const auto parsed = parse_line(view);
        if (!parsed) {
            errors << "line " << line_no << ": PARSE_ERROR: " << view << '\n';
            ++rejected;
            continue;
        }
        const Engine::RejectReason reason =
            exchange.submit(parsed->symbol, parsed->request);
        if (reason != Engine::RejectReason::None) {
            // Synchronous reject (bad field or unknown symbol). Asynchronous
            // rejects (duplicate / unknown client order id) are logged by the
            // symbol's Sink to <symbol>.log instead.
            errors << "line " << line_no << ": " << Engine::to_string(reason)
                   << ": " << view << '\n';
            ++rejected;
        } else {
            ++accepted;
        }
    }

    // stop() drains every worker's queue, joins the matching threads, then closes
    // and joins the Sinks -- so all per-symbol logs are fully flushed on return.
    exchange.stop();

    std::cout << "processed " << line_no << " lines: " << accepted
              << " accepted, " << rejected << " rejected (synchronous)\n"
              << "output written to " << out_dir << '\n';
    return 0;
}
