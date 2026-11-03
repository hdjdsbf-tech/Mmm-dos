#include "MacroFormat.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace fp {

std::string jsonEscape(std::string const& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (unsigned char ch : input) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) out += ' ';
                else out += static_cast<char>(ch);
        }
    }
    return out;
}

namespace {

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    Json const* get(std::string_view key) const {
        if (type != Obj) return nullptr;
        for (auto const& [k, v] : o) if (k == key) return &v;
        return nullptr;
    }
};

class Parser {
public:
    explicit Parser(std::string_view text) : m_s(text) {}

    bool parse(Json& out, std::string& error) {
        skip();
        if (!value(out)) { error = m_err; return false; }
        skip();
        if (m_i != m_s.size()) { error = "Unexpected data after JSON root"; return false; }
        return true;
    }

private:
    std::string_view m_s;
    std::size_t m_i = 0;
    int m_depth = 0;
    std::string m_err;

    bool bad(char const* msg) { if (m_err.empty()) m_err = msg; return false; }
    void skip() {
        while (m_i < m_s.size() && (m_s[m_i] == ' ' || m_s[m_i] == '\n' || m_s[m_i] == '\r' || m_s[m_i] == '\t')) ++m_i;
    }
    bool lit(std::string_view w) {
        if (m_s.substr(m_i, w.size()) == w) { m_i += w.size(); return true; }
        return false;
    }

    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }

    bool str(std::string& out) {
        if (m_i >= m_s.size() || m_s[m_i] != '"') return bad("Expected string");
        ++m_i;
        while (m_i < m_s.size()) {
            char c = m_s[m_i++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return bad("Control character in string");
            if (c != '\\') { out += c; continue; }
            if (m_i >= m_s.size()) break;
            char e = m_s[m_i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (m_i + 4 > m_s.size()) return bad("Bad unicode escape");
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = m_s[m_i++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= h - '0';
                        else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                        else return bad("Bad unicode escape");
                    }
                    if (cp >= 0xD800 && cp <= 0xDFFF) cp = '?';
                    utf8(out, cp);
                    break;
                }
                default: return bad("Bad escape");
            }
        }
        return bad("Unterminated string");
    }

    bool value(Json& out) {
        if (m_i >= m_s.size()) return bad("Unexpected end of JSON");
        if (++m_depth > 64) return bad("JSON nesting too deep");
        struct Guard { int& d; ~Guard() { --d; } } guard{m_depth};
        char c = m_s[m_i];
        if (c == '{') {
            ++m_i; out.type = Json::Obj; skip();
            if (m_i < m_s.size() && m_s[m_i] == '}') { ++m_i; return true; }
            while (true) {
                skip();
                std::string key;
                if (!str(key)) return false;
                skip();
                if (m_i >= m_s.size() || m_s[m_i] != ':') return bad("Expected ':'");
                ++m_i; skip();
                Json v;
                if (!value(v)) return false;
                out.o.emplace_back(std::move(key), std::move(v));
                skip();
                if (m_i < m_s.size() && m_s[m_i] == ',') { ++m_i; continue; }
                if (m_i < m_s.size() && m_s[m_i] == '}') { ++m_i; return true; }
                return bad("Expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++m_i; out.type = Json::Arr; skip();
            if (m_i < m_s.size() && m_s[m_i] == ']') { ++m_i; return true; }
            while (true) {
                skip();
                Json v;
                if (!value(v)) return false;
                out.a.push_back(std::move(v));
                skip();
                if (m_i < m_s.size() && m_s[m_i] == ',') { ++m_i; continue; }
                if (m_i < m_s.size() && m_s[m_i] == ']') { ++m_i; return true; }
                return bad("Expected ',' or ']'");
            }
        }
        if (c == '"') { out.type = Json::Str; return str(out.s); }
        if (lit("true")) { out.type = Json::Bool; out.b = true; return true; }
        if (lit("false")) { out.type = Json::Bool; out.b = false; return true; }
        if (lit("null")) { out.type = Json::Null; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            std::size_t start = m_i;
            if (m_s[m_i] == '-') ++m_i;
            while (m_i < m_s.size() && ((m_s[m_i] >= '0' && m_s[m_i] <= '9') || m_s[m_i] == '.' ||
                   m_s[m_i] == 'e' || m_s[m_i] == 'E' || m_s[m_i] == '+' || m_s[m_i] == '-')) ++m_i;
            std::string tmp(m_s.substr(start, m_i - start));
            char* end = nullptr;
            out.n = std::strtod(tmp.c_str(), &end);
            if (end != tmp.c_str() + tmp.size() || !std::isfinite(out.n)) return bad("Bad number");
            out.type = Json::Num;
            return true;
        }
        return bad("Unexpected character");
    }
};

LoadResult fail(LoadStatus status, std::string message) {
    LoadResult r;
    r.status = status;
    r.message = std::move(message);
    return r;
}

bool asUInt(Json const* j, std::uint32_t& out) {
    if (!j || j->type != Json::Num) return false;
    if (j->n < 0 || j->n > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) return false;
    if (j->n != std::floor(j->n)) return false;
    out = static_cast<std::uint32_t>(j->n);
    return true;
}

bool parseInputs(Json const& arr, std::vector<InputEvent>& out, std::string& error) {
    if (arr.type != Json::Arr) { error = "\"inputs\" must be an array"; return false; }
    if (arr.a.size() > MacroRecorder::MaxEvents) { error = "Too many input events"; return false; }
    out.reserve(arr.a.size());
    std::uint32_t last = 0;
    for (std::size_t i = 0; i < arr.a.size(); ++i) {
        auto const& e = arr.a[i];
        auto idx = std::to_string(i);
        if (e.type != Json::Obj) { error = "Input #" + idx + " is not an object"; return false; }
        InputEvent ev;
        if (!asUInt(e.get("frame"), ev.frame)) { error = "Input #" + idx + ": invalid frame"; return false; }
        auto const* btn = e.get("button");
        if (!btn) btn = e.get("btn");
        std::uint32_t b = 0;
        if (!asUInt(btn, b) || b < 1 || b > 8) { error = "Input #" + idx + ": button must be 1..8"; return false; }
        ev.button = static_cast<int>(b);
        auto const* down = e.get("down");
        if (!down || down->type != Json::Bool) { error = "Input #" + idx + ": invalid \"down\""; return false; }
        ev.down = down->b;
        auto const* p2 = e.get("player2");
        if (!p2) p2 = e.get("2p");
        if (p2 && p2->type != Json::Bool) { error = "Input #" + idx + ": invalid player flag"; return false; }
        ev.player2 = p2 && p2->b;
        if (ev.frame < last) { error = "Input #" + idx + ": frames are not sorted"; return false; }
        last = ev.frame;
        out.push_back(ev);
    }
    return true;
}

} // namespace

LoadResult loadMacroFile(std::filesystem::path const& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) return fail(LoadStatus::Unreadable, "Cannot read file: " + ec.message());
    if (size == 0) return fail(LoadStatus::Corrupt, "File is empty");
    if (size > 256ull * 1024 * 1024) return fail(LoadStatus::Corrupt, "File is too large");

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return fail(LoadStatus::Unreadable, "Cannot open file");
    std::string text((std::istreambuf_iterator<char>(in)), {});
    if (in.bad()) return fail(LoadStatus::Unreadable, "Read error");

    std::size_t p = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) p = 3;
    while (p < text.size() && (text[p] == ' ' || text[p] == '\n' || text[p] == '\r' || text[p] == '\t')) ++p;
    if (p >= text.size() || text[p] != '{') {
        auto ext = path.extension().string();
        if (ext == ".gdr" || ext == ".gdr2")
            return fail(LoadStatus::BinaryNotSupported,
                        "Binary " + ext + " files are not supported yet. Export the macro as .gdr.json.");
        return fail(LoadStatus::UnsupportedFormat, "Unknown file format (expected JSON)");
    }

    Json root;
    std::string err;
    Parser parser(std::string_view(text).substr(p));
    if (!parser.parse(root, err)) return fail(LoadStatus::Corrupt, "Corrupt JSON: " + err);
    if (root.type != Json::Obj) return fail(LoadStatus::Corrupt, "JSON root is not an object");

    Macro macro;
    auto const* inputs = root.get("inputs");
    if (!inputs) return fail(LoadStatus::UnsupportedFormat, "No \"inputs\" array: not a macro file");

    auto const* fmt = root.get("format");
    if (fmt) {
        if (fmt->type != Json::Str || fmt->s != "frame-preview-macro")
            return fail(LoadStatus::UnsupportedFormat, "Unknown \"format\" value");
        std::uint32_t schema = 0;
        if (!asUInt(root.get("schema"), schema) || schema != 1)
            return fail(LoadStatus::UnsupportedFormat, "Unsupported schema version");
        macro.sourceFormat = "fpm";
        auto const* c = root.get("complete");
        macro.complete = c && c->type == Json::Bool && c->b;
        asUInt(root.get("levelId"), macro.info.levelId);
        if (auto const* n = root.get("levelName"); n && n->type == Json::Str) macro.info.levelName = n->s;
        auto const* tps = root.get("tps");
        if (!tps || tps->type != Json::Num) return fail(LoadStatus::Corrupt, "Missing tps");
        macro.info.tps = static_cast<float>(tps->n);
        if (auto const* sm = root.get("speedMultiplier"); sm && sm->type == Json::Num)
            macro.info.speedMultiplier = static_cast<float>(sm->n);
    } else if (root.get("framerate") || root.get("level") || root.get("bot")) {
        macro.sourceFormat = "gdr-json";
        macro.complete = false; // GDR has no completion flag; do not claim it
        auto const* fr = root.get("framerate");
        if (!fr || fr->type != Json::Num) return fail(LoadStatus::Corrupt, "GDR: missing framerate");
        macro.info.tps = static_cast<float>(fr->n);
        if (auto const* lvl = root.get("level"); lvl && lvl->type == Json::Obj) {
            asUInt(lvl->get("id"), macro.info.levelId);
            if (auto const* n = lvl->get("name"); n && n->type == Json::Str) macro.info.levelName = n->s;
        }
    } else {
        return fail(LoadStatus::UnsupportedFormat, "Unrecognized JSON macro format");
    }

    if (!std::isfinite(macro.info.tps) || macro.info.tps < 1.f || macro.info.tps > 1000.f)
        return fail(LoadStatus::Corrupt, "TPS/framerate outside 1..1000");
    if (!std::isfinite(macro.info.speedMultiplier) || macro.info.speedMultiplier <= 0.f)
        macro.info.speedMultiplier = 1.f;

    if (!parseInputs(*inputs, macro.inputs, err)) return fail(LoadStatus::Corrupt, err);

    if (macro.sourceFormat == "fpm") {
        std::uint32_t count = 0;
        if (asUInt(root.get("eventCount"), count) && count != macro.inputs.size())
            return fail(LoadStatus::Corrupt, "eventCount does not match the number of inputs");
        asUInt(root.get("durationFrames"), macro.durationFrames);
    }
    if (!macro.inputs.empty() && macro.durationFrames < macro.inputs.back().frame)
        macro.durationFrames = macro.inputs.back().frame;

    macro.info.levelName = macro.info.levelName.substr(0, 256);
    LoadResult r;
    r.status = LoadStatus::Ok;
    r.macro = std::move(macro);
    return r;
}

bool writeMacroFile(std::filesystem::path const& path, Macro const& m) {
    if (path.empty()) return false;
    std::error_code ec;
    auto const parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;
        file.precision(9);
        file << "{\n"
             << "  \"format\": \"frame-preview-macro\",\n"
             << "  \"schema\": 1,\n"
             << "  \"complete\": " << (m.complete ? "true" : "false") << ",\n"
             << "  \"partial\": " << (m.complete ? "false" : "true") << ",\n"
             << "  \"levelId\": " << m.info.levelId << ",\n"
             << "  \"levelName\": \"" << jsonEscape(m.info.levelName) << "\",\n"
             << "  \"tps\": " << m.info.tps << ",\n"
             << "  \"speedMultiplier\": " << m.info.speedMultiplier << ",\n"
             << "  \"durationFrames\": " << m.durationFrames << ",\n"
             << "  \"eventCount\": " << m.inputs.size() << ",\n"
             << "  \"inputs\": [\n";
        for (std::size_t i = 0; i < m.inputs.size(); ++i) {
            auto const& e = m.inputs[i];
            file << "    {\"frame\": " << e.frame << ", \"button\": " << e.button
                 << ", \"down\": " << (e.down ? "true" : "false")
                 << ", \"player2\": " << (e.player2 ? "true" : "false") << "}";
            if (i + 1 != m.inputs.size()) file << ',';
            file << '\n';
        }
        file << "  ]\n}\n";
        file.flush();
        if (!file.good()) {
            file.close();
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

} // namespace fp
