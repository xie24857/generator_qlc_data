#include "ini/Ini.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

Ini& Ini::instance()
{
    static Ini config;
    return config;
}

void Ini::loadFile(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("无法打开文件: " + filename);
    values.clear();
    parse(file);
}
// ---- 解析核心 ----

void Ini::parse(std::istream& input)
{
    std::string line;
    std::string section;
    std::string prevName;
    int lineno = 0;
    int firstError = 0;

    while (std::getline(input, line)) {
        lineno++;

        // 去掉行尾 \r（兼容 Windows 换行）
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        // 第一行跳过 UTF-8 BOM
        if (lineno == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line = line.substr(3);
        }

        std::string stripped = trim(line);

        // 空行或注释行（; 或 # 开头）
        if (stripped.empty() || stripped[0] == ';' || stripped[0] == '#')
            continue;

        // 多行续行：行首有空白，且之前有 name
        if (!prevName.empty() && !line.empty() && std::isspace(static_cast<unsigned char>(line[0]))) {
            std::string val = stripInlineComment(stripped);
            val = rtrim(val);
            std::string key = makeKey(section, prevName);
            values[key] += "\n" + val;
            continue;
        }

        // [section] 行
        if (stripped[0] == '[') {
            std::size_t end = stripped.find(']');
            if (end == std::string::npos) {
                if (!firstError) firstError = lineno;
                continue;
            }
            section = trim(stripped.substr(1, end - 1));
            prevName.clear();
            continue;
        }

        // name=value 或 name:value
        std::size_t sep = stripped.find('=');
        if (sep == std::string::npos)
            sep = stripped.find(':');

        if (sep != std::string::npos) {
            std::string name = trim(stripped.substr(0, sep));
            std::string val = stripInlineComment(stripped.substr(sep + 1));
            val = trim(val);

            std::string key = makeKey(section, name);
            if (!values[key].empty())
                values[key] += "\n";
            values[key] += val;

            prevName = name;
        } else {
            // 既不是 section 也不是 name=value，记为错误
            if (!firstError) firstError = lineno;
        }
    }

    if (firstError)
        throw std::runtime_error("解析错误，第 " + std::to_string(firstError) + " 行");
}

// ---- 字符串工具 ----

std::string Ini::trim(const std::string& s)
{
    return ltrim(rtrim(s));
}

std::string Ini::ltrim(const std::string& s)
{
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        start++;
    return s.substr(start);
}

std::string Ini::rtrim(const std::string& s)
{
    std::size_t end = s.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(s[end - 1])))
        end--;
    return s.substr(0, end);
}

std::string Ini::stripInlineComment(const std::string& s)
{
    // 行内注释：; 前面必须有空白
    bool prevSpace = false;
    for (std::size_t i = 0; i < s.size(); i++) {
        if (prevSpace && s[i] == ';')
            return s.substr(0, i);
        prevSpace = std::isspace(static_cast<unsigned char>(s[i]));
    }
    return s;
}

std::string Ini::toLower(const std::string& s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

std::string Ini::makeKey(const std::string& section, const std::string& name)
{
    return toLower(section) + "=" + toLower(name);
}

// ---- 取值方法 ----

std::pair<bool, std::string> Ini::getString(const std::string& section,
                                                   const std::string& name) const
{
    auto it = values.find(makeKey(section, name));
    if (it != values.end())
        return std::make_pair(true, it->second);
    return std::make_pair(false, std::string());
}

std::pair<bool, long> Ini::getInt(const std::string& section,
                                         const std::string& name) const
{
    auto str = getString(section, name);
    if (!str.first)
        return std::make_pair(false, 0L);

    char* end;
    long n = std::strtol(str.second.c_str(), &end, 0);
    if (end == str.second.c_str())
        return std::make_pair(false, 0L);
    return std::make_pair(true, n);
}

std::pair<bool, double> Ini::getDouble(const std::string& section,
                                              const std::string& name) const
{
    auto str = getString(section, name);
    if (!str.first)
        return std::make_pair(false, 0.0);

    char* end;
    double n = std::strtod(str.second.c_str(), &end);
    if (end == str.second.c_str())
        return std::make_pair(false, 0.0);
    return std::make_pair(true, n);
}

std::pair<bool, bool> Ini::getBool(const std::string& section,
                                          const std::string& name) const
{
    auto str = getString(section, name);
    if (!str.first)
        return std::make_pair(false, false);

    std::string val = toLower(str.second);
    if (val == "true" || val == "yes" || val == "on" || val == "1")
        return std::make_pair(true, true);
    if (val == "false" || val == "no" || val == "off" || val == "0")
        return std::make_pair(true, false);
    return std::make_pair(false, false);
}

// ---- 查询方法 ----

bool Ini::hasSection(const std::string& section) const
{
    std::string prefix = makeKey(section, "");
    auto it = values.lower_bound(prefix);
    return it != values.end() && it->first.compare(0, prefix.size(), prefix) == 0;
}

bool Ini::hasKey(const std::string& section, const std::string& name) const
{
    return values.find(makeKey(section, name)) != values.end();
}

std::vector<std::string> Ini::sections() const
{
    std::set<std::string> result;
    for (const auto& pair : values) {
        std::size_t pos = pair.first.find('=');
        if (pos != std::string::npos)
            result.insert(pair.first.substr(0, pos));
    }
    return std::vector<std::string>(result.begin(), result.end());
}

std::vector<std::string> Ini::keys(const std::string& section) const
{
    std::vector<std::string> result;
    std::string prefix = makeKey(section, "");
    for (const auto& pair : values) {
        if (pair.first.compare(0, prefix.size(), prefix) == 0)
            result.push_back(pair.first.substr(prefix.size()));
    }
    return result;
}
