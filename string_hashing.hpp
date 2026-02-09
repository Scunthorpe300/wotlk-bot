#pragma once
#include <string>
#include <string_view>
#include <set>
#include <mutex>
#include <pthread.h>
#include <fmt/format.h>

#if defined(__cplusplus) && __cplusplus >= 202400L
#define HS_LIKELY    [[likely]]
#define HS_UNLIKELY  [[unlikely]]
#else
    #define HS_LIKELY
    #define HS_UNLIKELY
#endif


constexpr int HashedStringFn(std::string_view str)
{
    constexpr unsigned int FNV_PRIME = 16777619u;
    constexpr unsigned int FNV_OFFSET_BASIS = 2166136261u;
    unsigned int hash = FNV_OFFSET_BASIS;
    for (const char &c: str)
    {
        hash ^= static_cast<unsigned char>(c);
        hash *= FNV_PRIME;
    }
    return static_cast<int>(hash);
}

class HashedString
{

    std::string_view m_view;

    int m_hash;

    constexpr HashedString(std::string_view view, const int hash) :

    m_view(view),

    m_hash(hash)
    {
    }

    struct InterningManager
    {
        using StringPool = std::set<std::string>;
        StringPool pool;
        std::mutex mtx;

        InterningManager()
        {
            pthread_atfork(&InterningManager::on_fork_prepare, &InterningManager::on_fork_parent,
                           &InterningManager::on_fork_child);
        }

        static void on_fork_prepare() { get_instance().mtx.lock(); }
        static void on_fork_parent() { get_instance().mtx.unlock(); }
        static void on_fork_child() { get_instance().mtx.unlock(); }
    };

    static InterningManager &get_instance()
    {
        static InterningManager instance;
        return instance;
    }

    static HashedString intern(const std::string &s)
    {
        thread_local bool is_already_interning = false;

        if (is_already_interning) HS_UNLIKELY
        {
            return {};
        }

        is_already_interning = true;
        struct Guard
        {
            bool &flag;
            ~Guard() { flag = false; }
        };
        Guard reentrancy_guard{is_already_interning};

        auto &manager = get_instance();
        std::lock_guard<std::mutex> lock(manager.mtx);

        if (auto it = manager.pool.find(s); it != manager.pool.end()) HS_LIKELY
        {
            return HashedString(*it, HashedStringFn(*it));
        }

        auto result = manager.pool.insert(s);
        return HashedString(*result.first, HashedStringFn(*result.first));
    }

public:
    HashedString() : m_hash(0)
    {
    }

    template<size_t N>
    consteval explicit
    HashedString(const char (&literal)[N]) :
   
    m_view(literal, (N > 0 && literal[N - 1] == '\0') ? N - 1 : N),

                                             m_hash(HashedStringFn(literal))
    {
        static_assert(N > 1, "HashedString cannot be constructed from an empty literal string.");
    }

    [[nodiscard]] static HashedString create(const std::string &s)
    {
        if (s.empty()) HS_UNLIKELY
        {
            return {};
        }
        return intern(s);
    }

    [[nodiscard]] static HashedString create(std::string &&s)
    {
        if (s.empty()) HS_UNLIKELY
        {
            return {};
        }
        return intern(s);
    }

    
    [[nodiscard]] constexpr std::string_view get_view() const { return m_view; }
    
    [[nodiscard]] constexpr int get_hash() const { return m_hash; }

    [[nodiscard]] bool operator==(const HashedString &other) const
    {
        if (m_hash != other.m_hash) HS_LIKELY
        {
            return false;
        }

        return true;
    }
};

#ifdef DEBUG
namespace fmt
{
    template<>
    struct formatter<HashedString> : formatter<std::string_view>
    {
        template<typename FormatContext>
        auto format(const HashedString &hs, FormatContext &ctx) const
        {
            return formatter<std::string_view>::format(hs.get_view(), ctx);
        }
    };
}
#endif