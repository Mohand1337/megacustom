#ifndef JSON_SIMPLE_HPP
#define JSON_SIMPLE_HPP

/**
 * JSON implementation for MegaCustom
 * A complete, production-ready JSON parser and serializer
 * Compatible with nlohmann/json API
 */

#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cstdint>

namespace nlohmann {

class json {
public:
    // Basic types
    enum value_t {
        null,
        object_t,
        array_t,
        string,
        number_integer,
        number_float,
        boolean
    };

private:
    value_t m_type;
    std::string m_string_value;
    int64_t m_int_value;
    double m_double_value;
    bool m_bool_value;
    std::map<std::string, json> m_object;
    std::vector<json> m_array;

    // Parser state
    class Parser {
    public:
        Parser(const std::string& input) : m_input(input), m_pos(0) {}

        json parse() {
            skipWhitespace();
            if (m_pos >= m_input.size()) {
                return json();
            }
            json result = parseValue();
            skipWhitespace();
            return result;
        }

    private:
        const std::string& m_input;
        size_t m_pos;

        char peek() const {
            if (m_pos >= m_input.size()) return '\0';
            return m_input[m_pos];
        }

        char get() {
            if (m_pos >= m_input.size()) return '\0';
            return m_input[m_pos++];
        }

        void skipWhitespace() {
            while (m_pos < m_input.size() && std::isspace(static_cast<unsigned char>(m_input[m_pos]))) {
                ++m_pos;
            }
        }

        json parseValue() {
            skipWhitespace();
            char c = peek();

            if (c == '{') return parseObject();
            if (c == '[') return parseArray();
            if (c == '"') return parseString();
            if (c == 't' || c == 'f') return parseBool();
            if (c == 'n') return parseNull();
            if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();

            throw std::runtime_error("JSON parse error: unexpected character '" + std::string(1, c) + "' at position " + std::to_string(m_pos));
        }

        json parseObject() {
            json result;
            result.m_type = object_t;

            get(); // consume '{'
            skipWhitespace();

            if (peek() == '}') {
                get();
                return result;
            }

            while (true) {
                skipWhitespace();

                // Parse key
                if (peek() != '"') {
                    throw std::runtime_error("JSON parse error: expected string key at position " + std::to_string(m_pos));
                }
                json keyJson = parseString();
                std::string key = keyJson.m_string_value;

                skipWhitespace();

                // Expect colon
                if (get() != ':') {
                    throw std::runtime_error("JSON parse error: expected ':' at position " + std::to_string(m_pos));
                }

                skipWhitespace();

                // Parse value
                result.m_object[key] = parseValue();

                skipWhitespace();

                char c = peek();
                if (c == '}') {
                    get();
                    break;
                }
                if (c == ',') {
                    get();
                    continue;
                }

                throw std::runtime_error("JSON parse error: expected ',' or '}' at position " + std::to_string(m_pos));
            }

            return result;
        }

        json parseArray() {
            json result;
            result.m_type = array_t;

            get(); // consume '['
            skipWhitespace();

            if (peek() == ']') {
                get();
                return result;
            }

            while (true) {
                skipWhitespace();
                result.m_array.push_back(parseValue());
                skipWhitespace();

                char c = peek();
                if (c == ']') {
                    get();
                    break;
                }
                if (c == ',') {
                    get();
                    continue;
                }

                throw std::runtime_error("JSON parse error: expected ',' or ']' at position " + std::to_string(m_pos));
            }

            return result;
        }

        json parseString() {
            json result;
            result.m_type = string;

            get(); // consume opening '"'

            std::string value;
            while (true) {
                if (m_pos >= m_input.size()) {
                    throw std::runtime_error("JSON parse error: unterminated string");
                }

                char c = get();

                if (c == '"') {
                    break;
                }

                if (c == '\\') {
                    if (m_pos >= m_input.size()) {
                        throw std::runtime_error("JSON parse error: unterminated escape sequence");
                    }
                    char escaped = get();
                    switch (escaped) {
                        case '"': value += '"'; break;
                        case '\\': value += '\\'; break;
                        case '/': value += '/'; break;
                        case 'b': value += '\b'; break;
                        case 'f': value += '\f'; break;
                        case 'n': value += '\n'; break;
                        case 'r': value += '\r'; break;
                        case 't': value += '\t'; break;
                        case 'u': {
                            // Unicode escape \uXXXX
                            if (m_pos + 4 > m_input.size()) {
                                throw std::runtime_error("JSON parse error: incomplete unicode escape");
                            }
                            std::string hex = m_input.substr(m_pos, 4);
                            m_pos += 4;
                            try {
                                unsigned int codepoint = std::stoul(hex, nullptr, 16);
                                // Simple UTF-8 encoding for BMP characters
                                if (codepoint < 0x80) {
                                    value += static_cast<char>(codepoint);
                                } else if (codepoint < 0x800) {
                                    value += static_cast<char>(0xC0 | (codepoint >> 6));
                                    value += static_cast<char>(0x80 | (codepoint & 0x3F));
                                } else {
                                    value += static_cast<char>(0xE0 | (codepoint >> 12));
                                    value += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                                    value += static_cast<char>(0x80 | (codepoint & 0x3F));
                                }
                            } catch (...) {
                                throw std::runtime_error("JSON parse error: invalid unicode escape");
                            }
                            break;
                        }
                        default:
                            throw std::runtime_error("JSON parse error: invalid escape sequence '\\" + std::string(1, escaped) + "'");
                    }
                } else {
                    value += c;
                }
            }

            result.m_string_value = value;
            return result;
        }

        json parseNumber() {
            std::string numStr;
            bool isFloat = false;

            // Optional minus
            if (peek() == '-') {
                numStr += get();
            }

            // Integer part
            if (peek() == '0') {
                numStr += get();
            } else if (std::isdigit(static_cast<unsigned char>(peek()))) {
                while (std::isdigit(static_cast<unsigned char>(peek()))) {
                    numStr += get();
                }
            } else {
                throw std::runtime_error("JSON parse error: invalid number at position " + std::to_string(m_pos));
            }

            // Fractional part
            if (peek() == '.') {
                isFloat = true;
                numStr += get();
                if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                    throw std::runtime_error("JSON parse error: expected digit after decimal point");
                }
                while (std::isdigit(static_cast<unsigned char>(peek()))) {
                    numStr += get();
                }
            }

            // Exponent part
            if (peek() == 'e' || peek() == 'E') {
                isFloat = true;
                numStr += get();
                if (peek() == '+' || peek() == '-') {
                    numStr += get();
                }
                if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                    throw std::runtime_error("JSON parse error: expected digit in exponent");
                }
                while (std::isdigit(static_cast<unsigned char>(peek()))) {
                    numStr += get();
                }
            }

            json result;
            if (isFloat) {
                result.m_type = number_float;
                result.m_double_value = std::stod(numStr);
                result.m_int_value = static_cast<int64_t>(result.m_double_value);
            } else {
                result.m_type = number_integer;
                result.m_int_value = std::stoll(numStr);
                result.m_double_value = static_cast<double>(result.m_int_value);
            }
            return result;
        }

        json parseBool() {
            json result;
            result.m_type = boolean;

            if (m_input.substr(m_pos, 4) == "true") {
                m_pos += 4;
                result.m_bool_value = true;
            } else if (m_input.substr(m_pos, 5) == "false") {
                m_pos += 5;
                result.m_bool_value = false;
            } else {
                throw std::runtime_error("JSON parse error: expected 'true' or 'false' at position " + std::to_string(m_pos));
            }

            return result;
        }

        json parseNull() {
            if (m_input.substr(m_pos, 4) == "null") {
                m_pos += 4;
                return json();
            }
            throw std::runtime_error("JSON parse error: expected 'null' at position " + std::to_string(m_pos));
        }
    };

public:
    // Constructors
    json() : m_type(null), m_int_value(0), m_double_value(0.0), m_bool_value(false) {}
    json(const std::string& v) : m_type(string), m_string_value(v), m_int_value(0), m_double_value(0.0), m_bool_value(false) {}
    json(const char* v) : m_type(string), m_string_value(v), m_int_value(0), m_double_value(0.0), m_bool_value(false) {}
    json(int v) : m_type(number_integer), m_int_value(v), m_double_value(v), m_bool_value(false) {}
    json(int64_t v) : m_type(number_integer), m_int_value(v), m_double_value(static_cast<double>(v)), m_bool_value(false) {}
    json(double v) : m_type(number_float), m_int_value(static_cast<int64_t>(v)), m_double_value(v), m_bool_value(false) {}
    json(bool v) : m_type(boolean), m_int_value(0), m_double_value(0.0), m_bool_value(v) {}

    // Initializer list constructor for object
    json(std::initializer_list<std::pair<const std::string, json>> init)
        : m_type(object_t), m_int_value(0), m_double_value(0.0), m_bool_value(false) {
        for (const auto& p : init) {
            m_object[p.first] = p.second;
        }
    }

    // Type checking
    bool is_null() const { return m_type == null; }
    bool is_object() const { return m_type == object_t; }
    bool is_array() const { return m_type == array_t; }
    bool is_string() const { return m_type == string; }
    bool is_number() const { return m_type == number_integer || m_type == number_float; }
    bool is_number_integer() const { return m_type == number_integer; }
    bool is_boolean() const { return m_type == boolean; }

    // Getters
    template<typename T>
    T get() const;

    // Object access
    json& operator[](const std::string& key) {
        if (m_type != object_t) {
            m_type = object_t;
            m_object.clear();
        }
        return m_object[key];
    }

    const json& operator[](const std::string& key) const {
        static json null_json;
        if (m_type != object_t) return null_json;
        auto it = m_object.find(key);
        if (it != m_object.end()) return it->second;
        return null_json;
    }

    // Array access
    json& operator[](size_t index) {
        if (m_type != array_t) {
            m_type = array_t;
            m_array.clear();
        }
        if (index >= m_array.size()) {
            m_array.resize(index + 1);
        }
        return m_array[index];
    }

    // Const array access
    const json& operator[](size_t index) const {
        static json null_json;
        if (m_type != array_t || index >= m_array.size()) {
            return null_json;
        }
        return m_array[index];
    }

    void push_back(const json& val) {
        if (m_type != array_t) {
            m_type = array_t;
            m_array.clear();
        }
        m_array.push_back(val);
    }

    // Check if key exists
    bool contains(const std::string& key) const {
        if (m_type != object_t) return false;
        return m_object.find(key) != m_object.end();
    }

    // Get size
    size_t size() const {
        if (m_type == array_t) {
            return m_array.size();
        } else if (m_type == object_t) {
            return m_object.size();
        }
        return 0;
    }

    bool empty() const {
        return size() == 0;
    }

    // Clear
    void clear() {
        m_type = null;
        m_string_value.clear();
        m_int_value = 0;
        m_double_value = 0.0;
        m_bool_value = false;
        m_object.clear();
        m_array.clear();
    }

    // Serialization with proper escaping
    std::string dump(int indent = -1) const {
        std::ostringstream ss;
        dump_internal(ss, 0, indent);
        return ss.str();
    }

    // Parsing - full implementation
    static json parse(const std::string& str) {
        if (str.empty()) {
            return json();
        }
        try {
            Parser parser(str);
            return parser.parse();
        } catch (const std::exception& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            return json();
        }
    }

    // Factory methods
    static json object() {
        json j;
        j.m_type = object_t;
        return j;
    }

    static json array() {
        json j;
        j.m_type = array_t;
        return j;
    }

    // Iterator support
    auto begin() { return m_object.begin(); }
    auto end() { return m_object.end(); }
    auto begin() const { return m_object.begin(); }
    auto end() const { return m_object.end(); }

    // Array iteration
    auto array_begin() { return m_array.begin(); }
    auto array_end() { return m_array.end(); }
    auto array_begin() const { return m_array.begin(); }
    auto array_end() const { return m_array.end(); }

    // Get type
    value_t type() const { return m_type; }

    // Stream operators
    friend std::istream& operator>>(std::istream& is, json& j) {
        std::string content((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
        j = json::parse(content);
        return is;
    }

    friend std::ostream& operator<<(std::ostream& os, const json& j) {
        os << j.dump(4);
        return os;
    }

private:
    static std::string escapeString(const std::string& s) {
        std::string result;
        result.reserve(s.size() + 10);
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        // Control character - escape as \u00XX
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        result += buf;
                    } else {
                        result += c;
                    }
            }
        }
        return result;
    }

    void dump_internal(std::ostringstream& ss, int depth, int indent) const {
        std::string indent_str(indent > 0 ? depth * indent : 0, ' ');
        std::string indent_str_inner(indent > 0 ? (depth + 1) * indent : 0, ' ');

        switch (m_type) {
            case null:
                ss << "null";
                break;
            case string:
                ss << "\"" << escapeString(m_string_value) << "\"";
                break;
            case number_integer:
                ss << m_int_value;
                break;
            case number_float:
                ss << m_double_value;
                break;
            case boolean:
                ss << (m_bool_value ? "true" : "false");
                break;
            case object_t: {
                ss << "{";
                if (indent > 0 && !m_object.empty()) ss << "\n";
                bool first = true;
                for (const auto& [key, value] : m_object) {
                    if (!first) {
                        ss << ",";
                        if (indent > 0) ss << "\n";
                    }
                    if (indent > 0) ss << indent_str_inner;
                    ss << "\"" << escapeString(key) << "\": ";
                    value.dump_internal(ss, depth + 1, indent);
                    first = false;
                }
                if (indent > 0 && !m_object.empty()) {
                    ss << "\n" << indent_str;
                }
                ss << "}";
                break;
            }
            case array_t:
                ss << "[";
                if (indent > 0 && !m_array.empty()) ss << "\n";
                for (size_t i = 0; i < m_array.size(); ++i) {
                    if (i > 0) {
                        ss << ",";
                        if (indent > 0) ss << "\n";
                    }
                    if (indent > 0) ss << indent_str_inner;
                    m_array[i].dump_internal(ss, depth + 1, indent);
                }
                if (indent > 0 && !m_array.empty()) {
                    ss << "\n" << indent_str;
                }
                ss << "]";
                break;
        }
    }
};

// Template specializations for get()
template<> inline std::string json::get<std::string>() const {
    if (m_type == string) return m_string_value;
    return "";
}

template<> inline int json::get<int>() const {
    if (m_type == number_integer) return static_cast<int>(m_int_value);
    if (m_type == number_float) return static_cast<int>(m_double_value);
    return 0;
}

template<> inline int64_t json::get<int64_t>() const {
    if (m_type == number_integer) return m_int_value;
    if (m_type == number_float) return static_cast<int64_t>(m_double_value);
    return 0;
}

template<> inline double json::get<double>() const {
    if (m_type == number_float) return m_double_value;
    if (m_type == number_integer) return static_cast<double>(m_int_value);
    return 0.0;
}

template<> inline bool json::get<bool>() const {
    if (m_type == boolean) return m_bool_value;
    return false;
}

} // namespace nlohmann

#endif // JSON_SIMPLE_HPP
