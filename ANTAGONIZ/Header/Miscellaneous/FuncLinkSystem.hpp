#ifndef __FUNC_LINK_SYSTEM__
#define __FUNC_LINK_SYSTEM__

#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstddef>

constexpr uint64_t fnv1a(const char* str, std::size_t len)
{
    uint64_t hash = 14695981039346656037ull;

    for (std::size_t i = 0; i < len; ++i)
    {
        hash ^= static_cast<uint64_t>(str[i]);
        hash *= 1099511628211ull;
    }

    return hash;
}

constexpr uint64_t operator"" _id(const char* str, std::size_t len)
{
    return fnv1a(str, len);
}

//exemple constexpr auto PLAYER_DEAD = "PlayerDead"_id;

class FuncLinkSystem
{
public:
    using LinkId = uint64_t;
    using Callback = std::function<void()>;
private:
    std::unordered_map<LinkId, std::vector<Callback>> m_links;

public:
    static LinkId hash(std::string_view str)
    {
        uint64_t h = 14695981039346656037ull;

        for (char c : str)
        {
            h ^= static_cast<uint64_t>(c);
            h *= 1099511628211ull;
        }

        return h;
    }
    void addLink(LinkId id, Callback callback) { m_links[id].emplace_back(std::move(callback)); }
    void addLink(std::string_view tag, Callback callback) { addLink(hash(tag), std::move(callback)); }
    void removeLink(LinkId id) { m_links.erase(id); }
    void removeLink(std::string_view tag) { removeLink(hash(tag)); }
    void clearLink(LinkId id)
    {
        auto it = m_links.find(id);
        if (it != m_links.end()){it->second.clear();}
    }
    void clearLink(std::string_view tag) { clearLink(hash(tag)); }
    void clearAllLink() { m_links.clear(); }

    void execute(LinkId id)
    {
        auto it = m_links.find(id);
        if (it == m_links.end()) { return; }
        for (auto& callback : it->second) { callback(); }
    }

    void execute(std::string_view tag) { execute(hash(tag)); }
    bool hasLink(LinkId id) const { return m_links.find(id) != m_links.end(); }
    bool hasLink(std::string_view tag) const { return hasLink(hash(tag)); }

    std::size_t callbackCount(LinkId id) const
    {
        auto it = m_links.find(id);
        if (it == m_links.end()) { return 0; }
        return it->second.size();
    }

    std::size_t callbackCount(std::string_view tag) const
    {
        return callbackCount(hash(tag));
    }
};

#endif //!__FUNC_LINK_SYSTEM__