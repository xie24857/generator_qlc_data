#ifndef INI_H
#define INI_H

#include <string>
#include <map>
#include <vector>
#include <utility>
#include <stdexcept>

/// INI 配置文件解析器
/// 用法：
///   auto config = Ini::fromFile("config.ini");
///   auto result = config.getString("user", "name");
///   if (result.first) { /* 找到了，值是 result.second */ }
class Ini
{
public:
    static Ini& instance();
    void loadFile(const std::string& filename);

    /// 获取字符串值，first=是否找到，second=值
    std::pair<bool, std::string> getString(const std::string& section,
                                           const std::string& name) const;

    /// 获取整数值，first=是否找到且格式正确，second=值
    std::pair<bool, long> getInt(const std::string& section,
                                 const std::string& name) const;

    /// 获取浮点值，first=是否找到且格式正确，second=值
    std::pair<bool, double> getDouble(const std::string& section,
                                      const std::string& name) const;

    /// 获取布尔值（支持 true/false/yes/no/on/off/1/0）
    std::pair<bool, bool> getBool(const std::string& section,
                                  const std::string& name) const;

    /// 是否存在某个 section
    bool hasSection(const std::string& section) const;

    /// 是否存在某个键
    bool hasKey(const std::string& section, const std::string& name) const;

    /// 获取所有 section 名
    std::vector<std::string> sections() const;

    /// 获取某个 section 下所有 key
    std::vector<std::string> keys(const std::string& section) const;

private:
    Ini() = default;

    std::map<std::string, std::string> values;

    /// 解析核心逻辑
    void parse(std::istream& input);

    /// 去除字符串两端空白
    static std::string trim(const std::string& s);

    /// 去除字符串左端空白
    static std::string ltrim(const std::string& s);

    /// 去除字符串右端空白
    static std::string rtrim(const std::string& s);

    /// 去除行内注释（; 前需有空白）
    static std::string stripInlineComment(const std::string& s);

    /// 生成 map 的 key：section=name，全部小写
    static std::string makeKey(const std::string& section, const std::string& name);

    /// 转小写
    static std::string toLower(const std::string& s);
};

#endif // INI_H
