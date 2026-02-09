#pragma once

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include "string_hashing.hpp"
#include "fmt/color.h"

bool alternate_taunt = true;


#ifdef DEBUG
template<typename... Args>
void logWarn(fmt::format_string<Args...> fmt_str, Args &&... args)
{
    fmt::println("Warn: {}", fmt::format(fmt_str, std::forward<Args>(args)...));
}

template<typename... Args>
void logInfo(fmt::format_string<Args...> fmt_str, Args &&... args)
{
    fmt::println("Info: {}", fmt::format(fmt_str, std::forward<Args>(args)...));
}

template<typename... Args>
void logAll(fmt::format_string<Args...> fmt_str, Args &&... args)
{
    fmt::println("Log: {}", fmt::format(fmt_str, std::forward<Args>(args)...));
}

#else
template<typename... Args>
void logWarn(fmt::format_string<Args...>, Args &&...)
{
}

template<typename... Args>
void logInfo(fmt::format_string<Args...>, Args &&...)
{
}

template<typename... Args>
void logAll(fmt::format_string<Args...>, Args &&...)
{
}
#endif

enum STATUS_STATE
{
    WAITING_FOR_WOW, 
    WAITING_FOR_LOGIN, 
    READY 
};

STATUS_STATE STATUS = WAITING_FOR_WOW;
pid_t wow_pid = 0; 
Window wow_window;

inline bool read_memory(const uintptr_t address, void *buf, const size_t size)
{
#ifdef DEBUG
    assert(size != 0);
#endif
    if (wow_pid == 0)
    {
        return false;
    }

    struct iovec local[1];
    struct iovec remote[1];

    ssize_t nread;
    pid_t pid = wow_pid;

    local[0].iov_base = buf;
    local[0].iov_len = size;
    remote[0].iov_base = (void *) address;
    remote[0].iov_len = size;

    
    nread = process_vm_readv(pid, local, 1, remote, 1, 0);
    if (nread == -1)
    {
        int errsv = errno; 
        switch (errsv)
        {
            case EINVAL:
                {
                    logWarn("ERROR: INVALID ARGUMENTS.\n");
                    return false;
                }
            case EFAULT:
                {
                    
                    return false;
                }
            case ENOMEM:
                {
                    logWarn("ERROR: UNABLE TO ALLOCATE MEMORY.\n");
                    return false;
                }
            case EPERM:
                {
                    logWarn("ERROR: INSUFFICIENT PRIVILEGES TO TARGET PROCESS.\n");
                    return false;
                }
            case ESRCH:
                {
                    logWarn("ERROR: PROCESS DOES NOT EXIST.\n");
                    STATUS = WAITING_FOR_WOW;
                    wow_pid = 0;
                    wow_window = 0;
                    return false;
                }
            default:
                {
                    logWarn("ERROR: AN UNKNOWN ERROR HAS OCCURRED.\n");
                    return false;
                }
        }
    }

    return true;
}

inline bool write_memory(const uintptr_t address, void *buf, const size_t size)
{
    if (wow_pid == 0 || size == 0)
    {
        return false;
    }

    struct iovec local[1];
    struct iovec remote[1];

    size_t total_written = 0;
    char *buffer_ptr = static_cast<char *>(buf);

    constexpr int MAX_ITERATIONS = 100; 
    int iterations = 0;

    while (total_written < size)
    {
        if (++iterations > MAX_ITERATIONS)
        {
            logWarn("ERROR: write_memory exceeded max iterations. Wrote {}/{} bytes.\n", total_written, size);
            return false;
        }

        size_t remaining = size - total_written;

        local[0].iov_base = buffer_ptr + total_written;
        local[0].iov_len = remaining;
        remote[0].iov_base = reinterpret_cast<void *>(address + total_written);
        remote[0].iov_len = remaining;

        ssize_t nwritten = process_vm_writev(wow_pid, local, 1, remote, 1, 0);

        if (nwritten == -1)
        {
            int errsv = errno;
            switch (errsv)
            {
                case EINVAL:
                    logWarn("ERROR: INVALID ARGUMENTS.\n");
                    return false;
                case EFAULT:
                    return false;
                case ENOMEM:
                    logWarn("ERROR: UNABLE TO ALLOCATE MEMORY.\n");
                    return false;
                case EPERM:
                    logWarn("ERROR: INSUFFICIENT PRIVILEGES TO TARGET PROCESS.\n");
                    return false;
                case ESRCH:
                    logWarn("ERROR: PROCESS DOES NOT EXIST.\n");
                    STATUS = WAITING_FOR_WOW;
                    wow_pid = 0;
                    wow_window = 0;
                    return false;
                default:
                    logWarn("ERROR: AN UNKNOWN ERROR HAS OCCURRED (errno={}).\n", errsv);
                    return false;
            }
        }

        if (nwritten == 0)
        {
            logWarn("ERROR: write_memory stalled (0 bytes written). Wrote {}/{} bytes.\n", total_written, size);
            return false;
        }

        total_written += static_cast<size_t>(nwritten);
    }

    return true;
}

template<typename T>
bool write_memory(const uint32_t address, T value)
{
    constexpr auto size = sizeof(T);
#ifdef DEBUG
    assert(size != 0);
    assert(size <= 16);
    assert(address > 0);
#endif

    if (write_memory(address, &value, size))
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool write_memory(const uint32_t address, const std::string &text)
{
    size_t writeSize = text.length() + 1;

    return write_memory(address, (void *) text.c_str(), writeSize);
}

template<typename T>
auto read_memory(const uintptr_t address)
{
    constexpr auto size = sizeof(T);

    assert(address > 0);

    T retval;

    if (read_memory(address, &retval, size))
    {
        return retval;
    }

    return T{0};
}

inline bool write_memory_string(const uintptr_t address, const std::string &input)
{
    if (wow_pid == 0)
    {
        return false;
    }

    if (input.empty())
    {
        
        char null_term = '\0';
        return write_memory(address, &null_term, 1);
    }

    struct iovec local[1];
    struct iovec remote[1];

    
    const size_t total_size = input.size() + 1;
    size_t total_written = 0;
    const char *buffer_ptr = input.c_str(); 

    while (total_written < total_size)
    {
        size_t remaining = total_size - total_written;

        local[0].iov_base = const_cast<char *>(buffer_ptr + total_written);
        local[0].iov_len = remaining;
        remote[0].iov_base = reinterpret_cast<void *>(address + total_written);
        remote[0].iov_len = remaining;

        ssize_t nwritten = process_vm_writev(wow_pid, local, 1, remote, 1, 0);

        if (nwritten == -1)
        {
            int errsv = errno;
            switch (errsv)
            {
                case EINVAL:
                    logWarn("ERROR: INVALID ARGUMENTS.\n");
                    return false;
                case EFAULT:
                    
                    return false;
                case ENOMEM:
                    logWarn("ERROR: UNABLE TO ALLOCATE MEMORY.\n");
                    return false;
                case EPERM:
                    logWarn("ERROR: INSUFFICIENT PRIVILEGES TO TARGET PROCESS.\n");
                    return false;
                case ESRCH:
                    logWarn("ERROR: PROCESS DOES NOT EXIST.\n");
                    STATUS = WAITING_FOR_WOW;
                    wow_pid = 0;
                    wow_window = 0;
                    return false;
                default:
                    logWarn("ERROR: AN UNKNOWN ERROR HAS OCCURRED.\n");
                    return false;
            }
        }

        if (nwritten == 0)
        {
            logWarn("ERROR: write_memory_string stalled. Wrote {}/{} bytes.\n", total_written, total_size);
            return false;
        }

        total_written += static_cast<size_t>(nwritten);
    }

    return true;
}




inline std::string read_memory(const uintptr_t address, const int maximum_length)
{
#ifdef DEBUG
    assert(maximum_length < 1024);
#endif
    if (wow_pid == 0)
    {
        return "";
    }

    iovec local[1];
    iovec remote[1];

    ssize_t nread;
    pid_t pid = wow_pid;

    char buffer_up[maximum_length + 1];
    local[0].iov_base = buffer_up;
    local[0].iov_len = maximum_length;
    remote[0].iov_base = (void *) address;
    remote[0].iov_len = maximum_length;

    
    nread = process_vm_readv(pid, local, 1, remote, 1, 0);

    if (nread == -1)
    {
        int errsv = errno; 
        switch (errsv)
        {
            case EINVAL:
                {
                    logWarn("ERROR: INVALID ARGUMENTS.\n");
                    return "";
                }
            case EFAULT:
                {
                    logWarn("ERROR: UNABLE TO ACCESS TARGET MEMORY ADDRESS.\n");
                    return "";
                }
            case ENOMEM:
                {
                    logWarn("ERROR: UNABLE TO ALLOCATE MEMORY.\n");
                    return "";
                }
            case EPERM:
                {
                    logWarn("ERROR: INSUFFICIENT PRIVILEGES TO TARGET PROCESS.\n");
                    return "";
                }
            case ESRCH:
                {
                    logWarn("ERROR: PROCESS DOES NOT EXIST.\n");
                    STATUS = WAITING_FOR_WOW;
                    wow_pid = 0;
                    wow_window = 0;
                    return "";
                }
            default:
                {
                    logWarn("ERROR: AN UNKNOWN ERROR HAS OCCURRED.\n");
                    return "";
                }
        }
    }

    
    
    
    
    
    

    size_t size_str = 0;
    for (; size_str < maximum_length; ++size_str)
    {
        if (buffer_up[size_str] != '\0')
        {
            continue;
        }
        else
        {
            break;
        }
    }

    if (size_str == 0)
    {
        return "";
    }
    if (size_str >= maximum_length)
    {
        
        buffer_up[maximum_length] = 0;
        size_str = maximum_length;
    }
    else
    {
        buffer_up[size_str] = 0;
    }

    

    return {buffer_up, size_str};
}

bool is_player_looting()
{
    
    unsigned char read = read_memory<unsigned char>(0xBFA8D8);
    if (read > 0)
    {
        return true;
    }
    return false;
}

#define LOWORD(l) ((WORD)(l))
#define HIWORD(l) ((WORD)(((DWORD)(l) >> 16) & 0xFFFF))
#define LOBYTE(w) ((BYTE)(w))
#define HIBYTE(w) ((BYTE)(((WORD)(w) >> 8) & 0xFF))

#include <algorithm>
#include <cctype>
#include <string>
#include "fmt/format.h"
#include <grp.h> 

inline bool isUserInGroup(const std::string &groupName)
{
    int ngroups = getgroups(0, NULL);
    std::vector<gid_t> groups(ngroups);
    getgroups(ngroups, groups.data());

    for (const auto &group: groups)
    {
        struct group *grp = getgrgid(group);
        if (!grp) continue;

        if (groupName == grp->gr_name)
        {
            return true;
        }
    }

    return false;
}


inline void ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch)
    {
        return !std::isspace(ch);
    }));
}


inline void rtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch)
    {
        return !std::isspace(ch);
    }).base(), s.end());
}


inline void trim(std::string &s)
{
    ltrim(s);
    rtrim(s);
}

static void toLowercase(std::string &s)
{
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
}

inline bool containsSubstring(const std::string_view mainStr, const std::string_view subStr)
{
    return mainStr.find(subStr) != std::string_view::npos;
}

ulong main_tank_guid = 0;
bool alternative_rotation_mode = false;
bool automatic_taunting = true;
bool automatic_flag = true;
bool showing_radar = false;
auto now = std::chrono::steady_clock::now();


class EventSystem
{
public:
    using interval_signal = boost::signals2::signal<void(void)>;
    interval_signal Signal;

    boost::signals2::connection connect(const interval_signal::slot_type &subscriber)
    {
        return Signal.connect(subscriber);
    }

    ~EventSystem()
    {
        Signal.disconnect_all_slots();
    }
};

namespace wotlkbotSettingsDB
{
    struct Settings
    {
        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version)
        {
            ar & selected_setting;
            ar & character_guid;
        }

    public:
        int selected_setting;
        ulong character_guid;
    };

    inline std::size_t hash_value(const Settings &rhs)
    {
        size_t seed = 0;
        boost::hash_combine(seed, rhs.selected_setting);
        boost::hash_combine(seed, rhs.character_guid);
        return seed;
    }
} 

namespace wotlkbotSettingsDB2
{
    struct Settings2
    {
        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive &ar, [[maybe_unused]] const unsigned int version)
        {
            ar & username;
            ar & password;
        }

    public:
        std::string username;
        std::string password;
    };

    inline std::size_t hash_value(const Settings2 &rhs)
    {
        size_t seed = 0;
        boost::hash_combine(seed, rhs.username);
        boost::hash_combine(seed, rhs.password);
        return seed;
    }
}

ardb::ArrayDatabaseFlat<wotlkbotSettingsDB2::Settings2> AccountSettings("ASDB");
ardb::ArrayDatabase<std::vector<wotlkbotSettingsDB::Settings> > CurrentlyActiveSettings("MasterSettingsDB");



class TimerBool
{
private:
    bool state;
    long when;

public:
    void begin()
    {
        state = true;
        when = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).
                count();
    }

    explicit operator bool()
    {
        if (!state)
        {
            return false;
        }
        auto timesince = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch()).count() - when;
        if (timesince <= 3)
        {
            return true;
        }
        if (timesince >= 3)
        {
            state = false;
            return false;
        }

        state = false;
        return false;
    }
};

class OneTimeTrueBool
{
private:
    bool state;

public:
    void begin()
    {
        state = true;
    }

    explicit operator bool()
    {
        if (!state)
        {
            return false;
        }
        if (state)
        {
            
            state = false;
            return true;
        }
        state = false;
        return false;
    }
};

struct Point
{
public:
    float x;
    float y;
    float z;

    
    [[nodiscard]] float Distance(const Point &p2) const
    {
        float deltaX = p2.x - x;
        float deltaY = p2.y - y;
        return std::sqrt(deltaX * deltaX + deltaY * deltaY);
    }

    [[nodiscard]] float DistanceTo(const Point &p2) const
    {
        if ((x == 0 && y == 0 && z == 0) || (p2.x == 0 && p2.y == 0 && p2.z == 0))
        {
            return MAXFLOAT;
        }
        return std::sqrt((x - p2.x) * (x - p2.x) + (y - p2.y) * (y - p2.y) + (z - p2.z) * (z - p2.z));
    }

    
    [[nodiscard]] Point Normalize() const
    {
        float length = std::sqrt(x * x + y * y + z * z);
        if (length > 0.0001f)
        {
            return {x / length, y / length, z / length};
        }
        return {0.0f, 0.0f, 0.0f};
    }

    
    static Point RotationToDirection(float rotation)
    {
        return {std::sin(rotation), 0.0f, std::cos(rotation)};
    }

    
    static float Dot(const Point &a, const Point &b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
};

template<>
struct fmt::formatter<Point>
{
    char presentation = 'f';

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin())
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && (*it == 'f' || *it == 'e'))
        {
            presentation = *++it;
        }

        if (it != end && *it != '}')
        {
            throw format_error("invalid format");
        }

        return it;
    }

    template<typename FormatContext>
    auto format(const Point &p, FormatContext &ctx) const -> decltype(ctx.out())
    {
        return presentation == 'f'
                   ? fmt::format_to(ctx.out(), "({:.1f}, {:.1f}, {:.1f})", p.x, p.y, p.z)
                   : fmt::format_to(ctx.out(), "({:.1e}, {:.1e}, {:.1f})", p.x, p.y, p.z);
    }
};


constexpr int char_to_int(char c)
{
    return (c >= '0' && c <= '9')
               ? c - '0'
               : (c >= 'a' && c <= 'f')
                     ? c - 'a' + 10
                     : (c >= 'A' && c <= 'F')
                           ? c - 'A' + 10
                           : 0;
}


constexpr int str_to_int(const char *str, std::size_t length)
{
    int value = 0;
    for (std::size_t i = 0; i < length; ++i)
    {
        value = value * 16 + char_to_int(str[i]);
    }
    return value;
}

#define external_pointer static constexpr unsigned int const

external_pointer playername(0x00C79D18);
external_pointer object_manager(0xC79CE0);
external_pointer PlayerGUID1(0x00C79D10);
external_pointer PlayerGUID2(0x00CA1238);

external_pointer PLAYER_POS_X(0xADF4E4);
external_pointer PLAYER_POS_Y(0xADF4E8);
external_pointer PLAYER_POS_Z(0xADF4EC);

external_pointer CLIENT_CONNECTION(0xC79CE0);
external_pointer OBJ_MANAGER(0x2ED0); 
external_pointer LIST_START(0xAC); 


external_pointer NEXT(0x3C); 
external_pointer DESC(0x8);
external_pointer TYPE(0x14);
external_pointer GUID(0x30);
external_pointer POS_X(0x798);
external_pointer POS_Y(POS_X + 0x4);
external_pointer POS_Z(POS_X + 0x8);
external_pointer POS_R(POS_X + 0x10);
external_pointer POS_P(POS_X + 0x14);
external_pointer BOBBING(0xBC);

external_pointer HEALTH_OFFSET(0xFB0);
external_pointer LastHardwareAction(0xB499A4);

external_pointer ComboPoints(0xBD084D); 


external_pointer RuneState(0xC24388);
external_pointer RuneType(0xC24304);
external_pointer RuneCooldown(0xC24364);

external_pointer PartyArray(0x00BD1948);
external_pointer DungeonDifficulty(0x00BD0898); 
external_pointer RaidCount(0x00BEB608);
external_pointer RaidArray(0x00BEB568);
external_pointer RaidDifficulty(0x00BD089C);

external_pointer AURA_COUNT_1(0xDD0); 
external_pointer AURA_COUNT_2(0xC54); 
external_pointer AURA_TABLE_1(0xC50); 
external_pointer AURA_TABLE_2(0xC58); 
external_pointer AURA_SIZE(0x18); 
external_pointer AURA_SPELL_OWNER(0x0); 
external_pointer AURA_SPELL_ID(0x8); 

external_pointer AURA_SPELL_STACKCOUNT(0x0E); 

external_pointer AURA_SPELL_ENDTIME(0x14); 

external_pointer AURA_SPELL_FLAGS(0x9); 
external_pointer AURA_SPELL_LEVEL(0x10); 
external_pointer AURA_SPELL_DURATION(0x16); 
external_pointer AURA_SPELL_EFFECT_FLAGS(0x1C); 

external_pointer AURA_SPELL_TYPE(0x24); 
external_pointer AURA_SPELL_CASTER(0x28); 
external_pointer AURA_EFFECT_ID(0x30); 

constexpr size_t SPELL_RECORD_UNCOMPRESSED_SIZE = 0x2A8; 
std::vector<char> DecompressSpellRecord(uint32_t compressed_ptr)
{
    std::vector<char> decompressed_data;
    decompressed_data.reserve(SPELL_RECORD_UNCOMPRESSED_SIZE);

    uint32_t source_cursor = compressed_ptr;
    char prev_byte = 0;

    while (decompressed_data.size() < SPELL_RECORD_UNCOMPRESSED_SIZE)
    {
        char current_byte = read_memory<char>(source_cursor++);

        
        
        if (!decompressed_data.empty() && current_byte == prev_byte)
        {
            
            uint8_t count = read_memory<uint8_t>(source_cursor++);

            
            for (uint8_t i = 0; i < count; ++i)
            {
                decompressed_data.push_back(current_byte);
                if (decompressed_data.size() >= SPELL_RECORD_UNCOMPRESSED_SIZE)
                {
                    break; 
                }
            }
        }
        else 
        {
            decompressed_data.push_back(current_byte);
        }

        prev_byte = current_byte;
    }

    
    if (decompressed_data.size() > SPELL_RECORD_UNCOMPRESSED_SIZE)
    {
        decompressed_data.resize(SPELL_RECORD_UNCOMPRESSED_SIZE);
    }
    else if (decompressed_data.size() < SPELL_RECORD_UNCOMPRESSED_SIZE)
    {
        
        fmt::print("Warning: Decompression of spell at {:x} resulted in incomplete data ({} bytes)\n", compressed_ptr,
                   decompressed_data.size());
        return {}; 
    }

    return decompressed_data;
}


external_pointer SPELL_DATABASE_CACHE(0x00AD49D0);

int wow_timestamp = 0;
external_pointer TIMESTAMP(0xB1D618);


external_pointer IsActionBarSlotUsable(0xc1ded8);
external_pointer HasEnoughManaSlotUsable(0xc1ded8);




external_pointer nameBase(0x1C);
external_pointer nameMask(0x24);
external_pointer nameStore(0xC5D938 + 0x8);
external_pointer nameString(0x20);

external_pointer FirstActionBarSpellId(0xC1E358);
external_pointer FirstActionBarSpellIdDepths(0xC1E508); 


external_pointer FirstActionBarBearForm_and_WarriorBeserkerStance(0xC1E4D8);
external_pointer FirstActionBarCatForm_ShadowForm_and_WarriorBattleStance(0xC1E478);
external_pointer FirstActionBarRestoTreeForm_and_WarriorDefensiveStance(0xC1E4A8);
external_pointer FirstActionBarDruidStealth(0xC1E4A8);
external_pointer FirstActionBarRogueStealth(0xC1E478);


unsigned int selected_castbar_addr(0x0);

external_pointer PetGUID(0xC234D0);

external_pointer RVA_FactionIdBase = 0x00ad3894;
external_pointer RVA_FactionIdMax = 0x00ad3890;
external_pointer RVA_FactionDataTable = 0x00ad38a4;
external_pointer OFF_pUnitData = 0xD0;
external_pointer OFF_pOwnerObject = 0x140; 
external_pointer OFF_unitData_FactionID = 0xC4;
external_pointer OFF_unitData_Bitfield_Combatant = 0xD4;
external_pointer OFF_unitData_Bitfield_Pvp = 0x1D1;
external_pointer OFF_faction_ParentId = 0x04;
external_pointer OFF_faction_FlagsPvp = 0x08;
external_pointer OFF_faction_GroupMask = 0x0C;
external_pointer OFF_faction_AllyGroupFlags = 0x10;
external_pointer OFF_faction_HostileGroupFlags = 0x14;
external_pointer OFF_faction_ExplicitEnemies = 0x18;
external_pointer OFF_faction_ExplicitAllies = 0x28;

enum class Relationship
{
    Hostile,
    Neutral,
    Friendly
};

template<>
struct fmt::formatter<Relationship>
{
    constexpr auto parse(fmt::format_parse_context &ctx)
    {
        auto it = ctx.begin(), end = ctx.end();
        if (it != end && *it != '}')
        {
            throw fmt::format_error("invalid format");
        }
        return it;
    }

    template<typename FormatContext>
    auto format(Relationship rel, FormatContext &ctx) const
    {
        std::string_view name = "Unknown";
        switch (rel)
        {
            case Relationship::Hostile: name = "Hostile";
                break;
            case Relationship::Neutral: name = "Neutral";
                break;
            case Relationship::Friendly: name = "Friendly";
                break;
        }

        return std::copy(name.begin(), name.end(), ctx.out());
    }
};


int last_spell_out = 0;
unsigned long last_healer_target_guid = 0;

struct DeathKnightRunes
{
    int blood;
    int frost;
    int unholy;
    int death;
};

enum WowObjectType : int
{
    notype = 0,
    Item = 1,
    Container = 2,
    Unit = 3,
    Player = 4,
    normal_GameObject = 5,
    DynamicObject = 6,
    Corpse = 7,
    AiGroup = 8,
    AreaTrigger = 9
};

template<>
struct fmt::formatter<WowObjectType>
{
    constexpr auto parse(format_parse_context &ctx)
    {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const int &p, FormatContext &ctx) const -> decltype(ctx.out())
    {
        switch (p)
        {
            case 0:
                return fmt::format_to(ctx.out(), "NoType");
            case 1:
                return fmt::format_to(ctx.out(), "GOItem");
            case 2:
                return fmt::format_to(ctx.out(), "GOContainer");
            case 3:
                return fmt::format_to(ctx.out(), "GOUnit");
            case 4:
                return fmt::format_to(ctx.out(), "GOPlayer");
            case 5:
                return fmt::format_to(ctx.out(), "GONormal");
            case 6:
                return fmt::format_to(ctx.out(), "GODynamic");
            case 7:
                return fmt::format_to(ctx.out(), "GOCorpse");
            case 8:
                return fmt::format_to(ctx.out(), "GOBLIZZARDSCRIPT_AIGROUP");
            case 9:
                return fmt::format_to(ctx.out(), "GOBLIZZARDSCRIPT_AREATRIGGER");
            default:
                return fmt::format_to(ctx.out(), "GO_MISREAD_TYPE_ERROR");
        }
    }
};

struct aura_complete
{
    int spell_id = 0;
    int stack_count = 0;
    int time_left = 0;
    ulong spell_caster = 0;
    unsigned int opspell_hash = 0;
};

enum SpellFlags : uint32_t
{
    SPELL_FLAG_NONE = 0x00000000,
    SPELL_FLAG_PASSIVE = 0x00000001,
    SPELL_FLAG_CHANNELED = 0x00000002,
    SPELL_FLAG_POSITIVE = 0x00000008,
    SPELL_FLAG_DEBUFF = 0x00000010,
    SPELL_FLAG_TARGET_ENEMY = 0x00000100
};

enum AuraEffectFlags : uint8_t
{
    EFFECT_FLAG_NONE = 0x00,
    EFFECT_FLAG_PASSIVE = 0x01,
    EFFECT_FLAG_PERIODIC = 0x02,
    EFFECT_FLAG_POSITIVE = 0x08,
    EFFECT_FLAG_NEGATIVE = 0x10,
    EFFECT_FLAG_UNK7 = 0x80
};

struct GameObject
{
public:
    uint address; 
    uint descriptor;
    WowObjectType type;
    ulong guid;
    Point position;
    int health;
    int max_health;
    int health_pct; 
    std::vector<aura_complete> auras;
    bool priority = false;

    constexpr bool operator==(const GameObject &rhs) const
    {
        return guid == rhs.guid;
    }

    operator bool() const
    {
        return guid != 0;
    }
};

inline bool sortGameobject(const GameObject &go1, const GameObject &go2)
{
    return (go1.health_pct < go2.health_pct);
}

inline std::vector<GameObject> gameobjects_seen;



enum AuraSpellFlags : uint8_t
{
    AURA_FLAG_PASSIVE = 0x01, 
    AURA_FLAG_UNK1 = 0x02, 
    AURA_FLAG_UNK2 = 0x04, 
    AURA_FLAG_CONSOLIDATE = 0x08, 
    AURA_FLAG_STEALABLE = 0x10, 
    AURA_FLAG_UNK5 = 0x20, 
    AURA_FLAG_UNK6 = 0x40, 
    AURA_FLAG_HIDDEN = 0x80 
};

enum class DebuffType : uint8_t
{
    MAGIC = 0, 
    CURSE = 1, 
    DISEASE = 2, 
    POISON = 3, 
    NONE = 4 
};

template<>
struct fmt::formatter<DebuffType> : fmt::formatter<std::string_view>
{
    auto format(DebuffType dt, fmt::format_context &ctx) const
    {
        std::string_view name;
        switch (dt)
        {
            case DebuffType::MAGIC: name = "Magic";
                break;
            case DebuffType::CURSE: name = "Curse";
                break;
            case DebuffType::DISEASE: name = "Disease";
                break;
            case DebuffType::POISON: name = "Poison";
                break;
            case DebuffType::NONE: name = "None";
                break;
            default: name = "Unknown";
                break;
        }
        return fmt::formatter<std::string_view>::format(name, ctx);
    }
};


#define OBJECT_FIELD_GUID           0x00
#define OBJECT_FIELD_TYPE           0x10 
#define OBJECT_FIELD_SCALE          0x14 
#define GAMEOBJECT_DISPLAYID        0x20
#define GAMEOBJECT_FLAGS            0x24
#define GAMEOBJECT_FACTION          0x3C
#define GAMEOBJECT_BYTES_1          0x44 


enum GameObjectTypes
{
    GO_TYPE_DOOR = 0,
    GO_TYPE_BUTTON = 1,
    GO_TYPE_QUESTGIVER = 2,
    GO_TYPE_CHEST = 3, 
    GO_TYPE_BINDER = 4,
    GO_TYPE_GENERIC = 5,
    GO_TYPE_TRAP = 6,
    GO_TYPE_CHAIR = 7,
    GO_TYPE_SPELL_FOCUS = 8,
    GO_TYPE_TEXT = 9,
    GO_TYPE_GOOBER = 10,
    GO_TYPE_TRANSPORT = 11,
    GO_TYPE_AREADAMAGE = 12,
    GO_TYPE_CAMERA = 13,
    GO_TYPE_MAP_OBJECT = 14,
    GO_TYPE_MO_TRANSPORT = 15,
    GO_TYPE_DUEL_ARBITER = 16,
    GO_TYPE_FISHINGNODE = 17, 
    GO_TYPE_SUMMONING_RITUAL = 18,
    GO_TYPE_MAILBOX = 19,
    GO_TYPE_GATHERING_NODE = 50 
};

enum class WoWObjectFields : uint32_t
{
    GUID = 0x0,
    TYPE = 0x2,
    ENTRY = 0x3,
    SCALE_X = 0x4,
    PADDING = 0x5,
};

enum class WoWItemFields : uint32_t
{
    OWNER = 0x6,
    CONTAINED = 0x8,
    CREATOR = 0xA,
    GIFTCREATOR = 0xC,
    STACK_COUNT = 0xE,
    DURATION = 0xF,
    SPELL_CHARGES = 0x10,
    FLAGS = 0x15,
    ENCHANTMENT_1_1 = 0x16,
    ENCHANTMENT_1_3 = 0x18,
    ENCHANTMENT_2_1 = 0x19,
    ENCHANTMENT_2_3 = 0x1B,
    ENCHANTMENT_3_1 = 0x1C,
    ENCHANTMENT_3_3 = 0x1E,
    ENCHANTMENT_4_1 = 0x1F,
    ENCHANTMENT_4_3 = 0x21,
    ENCHANTMENT_5_1 = 0x22,
    ENCHANTMENT_5_3 = 0x24,
    ENCHANTMENT_6_1 = 0x25,
    ENCHANTMENT_6_3 = 0x27,
    ENCHANTMENT_7_1 = 0x28,
    ENCHANTMENT_7_3 = 0x2A,
    ENCHANTMENT_8_1 = 0x2B,
    ENCHANTMENT_8_3 = 0x2D,
    ENCHANTMENT_9_1 = 0x2E,
    ENCHANTMENT_9_3 = 0x30,
    ENCHANTMENT_10_1 = 0x31,
    ENCHANTMENT_10_3 = 0x33,
    ENCHANTMENT_11_1 = 0x34,
    ENCHANTMENT_11_3 = 0x36,
    ENCHANTMENT_12_1 = 0x37,
    ENCHANTMENT_12_3 = 0x39,
    PROPERTY_SEED = 0x3A,
    RANDOM_PROPERTIES_ID = 0x3B,
    DURABILITY = 0x3C,
    MAXDURABILITY = 0x3D,
    CREATE_PLAYED_TIME = 0x3E,
    PAD = 0x3F,
};

enum class WoWContainerFields : uint32_t
{
    NUM_SLOTS = 0x6,
    ALIGN_PAD = 0x7,
    SLOT_1 = 0x8,
};

enum class WoWGameObjectFields : uint32_t
{
    CREATED_BY = 0x6,
    DISPLAYID = 0x8,
    FLAGS = 0x9,
    PARENTROTATION = 0xA,
    DYNAMIC = 0xE,
    FACTION = 0xF,
    LEVEL = 0x10,
    BYTES_1 = 0x11,
};

enum class WoWDynamicObjectFields : uint32_t
{
    CASTER = 0x6,
    BYTES = 0x8,
    SPELLID = 0x9,
    RADIUS = 0xA,
    CASTTIME = 0xB,
};

enum class WoWCorpseFields : uint32_t
{
    OWNER = 0x6,
    PARTY = 0x8,
    DISPLAY_ID = 0xA,
    ITEM = 0xB,
    BYTES_1 = 0x1E,
    BYTES_2 = 0x1F,
    GUILD = 0x20,
    FLAGS = 0x21,
    DYNAMIC_FLAGS = 0x22,
    PAD = 0x23,
};

enum class eUnitFields : uint32_t
{
    UNIT_FIELD_CHARM = 0x6,
    UNIT_FIELD_SUMMON = 0x8,
    UNIT_FIELD_CRITTER = 0xA,
    UNIT_FIELD_CHARMEDBY = 0xC,
    UNIT_FIELD_SUMMONEDBY = 0xE,
    UNIT_FIELD_CREATEDBY = 0x10,
    UNIT_FIELD_TARGET = 0x12,
    UNIT_FIELD_CHANNEL_OBJECT = 0x14,
    UNIT_CHANNEL_SPELL = 0x16,
    UNIT_FIELD_BYTES_0 = 0x17,
    UNIT_FIELD_HEALTH = 0x18,
    UNIT_FIELD_POWER1 = 0x19,
    UNIT_FIELD_POWER2 = 0x1A,
    UNIT_FIELD_POWER3 = 0x1B,
    UNIT_FIELD_POWER4 = 0x1C,
    UNIT_FIELD_POWER5 = 0x1D,
    UNIT_FIELD_POWER6 = 0x1E,
    UNIT_FIELD_POWER7 = 0x1F,
    UNIT_FIELD_MAXHEALTH = 0x20,
    UNIT_FIELD_MAXPOWER1 = 0x21,
    UNIT_FIELD_MAXPOWER2 = 0x22,
    UNIT_FIELD_MAXPOWER3 = 0x23,
    UNIT_FIELD_MAXPOWER4 = 0x24,
    UNIT_FIELD_MAXPOWER5 = 0x25,
    UNIT_FIELD_MAXPOWER6 = 0x26,
    UNIT_FIELD_MAXPOWER7 = 0x27,
    UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER = 0x28,
    UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER = 0x2F,
    UNIT_FIELD_LEVEL = 0x36,
    UNIT_FIELD_FACTIONTEMPLATE = 0x37,
    UNIT_VIRTUAL_ITEM_SLOT_ID = 0x38,
    UNIT_FIELD_FLAGS = 0x3B,
    UNIT_FIELD_FLAGS_2 = 0x3C,
    UNIT_FIELD_AURASTATE = 0x3D,
    UNIT_FIELD_BASEATTACKTIME = 0x3E,
    UNIT_FIELD_RANGEDATTACKTIME = 0x40,
    UNIT_FIELD_BOUNDINGRADIUS = 0x41,
    UNIT_FIELD_COMBATREACH = 0x42,
    UNIT_FIELD_DISPLAYID = 0x43,
    UNIT_FIELD_NATIVEDISPLAYID = 0x44,
    UNIT_FIELD_MOUNTDISPLAYID = 0x45,
    UNIT_FIELD_MINDAMAGE = 0x46,
    UNIT_FIELD_MAXDAMAGE = 0x47,
    UNIT_FIELD_MINOFFHANDDAMAGE = 0x48,
    UNIT_FIELD_MAXOFFHANDDAMAGE = 0x49,
    UNIT_FIELD_BYTES_1 = 0x4A,
    UNIT_FIELD_PETNUMBER = 0x4B,
    UNIT_FIELD_PET_NAME_TIMESTAMP = 0x4C,
    UNIT_FIELD_PETEXPERIENCE = 0x4D,
    UNIT_FIELD_PETNEXTLEVELEXP = 0x4E,
    UNIT_DYNAMIC_FLAGS = 0x4F,
    UNIT_MOD_CAST_SPEED = 0x50,
    UNIT_CREATED_BY_SPELL = 0x51,
    UNIT_NPC_FLAGS = 0x52,
    UNIT_NPC_EMOTESTATE = 0x53,
    UNIT_FIELD_STAT0 = 0x54,
    UNIT_FIELD_STAT1 = 0x55,
    UNIT_FIELD_STAT2 = 0x56,
    UNIT_FIELD_STAT3 = 0x57,
    UNIT_FIELD_STAT4 = 0x58,
    UNIT_FIELD_POSSTAT0 = 0x59,
    UNIT_FIELD_POSSTAT1 = 0x5A,
    UNIT_FIELD_POSSTAT2 = 0x5B,
    UNIT_FIELD_POSSTAT3 = 0x5C,
    UNIT_FIELD_POSSTAT4 = 0x5D,
    UNIT_FIELD_NEGSTAT0 = 0x5E,
    UNIT_FIELD_NEGSTAT1 = 0x5F,
    UNIT_FIELD_NEGSTAT2 = 0x60,
    UNIT_FIELD_NEGSTAT3 = 0x61,
    UNIT_FIELD_NEGSTAT4 = 0x62,
    UNIT_FIELD_RESISTANCES = 0x63,
    UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE = 0x6A,
    UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE = 0x71,
    UNIT_FIELD_BASE_MANA = 0x78,
    UNIT_FIELD_BASE_HEALTH = 0x79,
    UNIT_FIELD_BYTES_2 = 0x7A,
    UNIT_FIELD_ATTACK_POWER = 0x7B,
    UNIT_FIELD_ATTACK_POWER_MODS = 0x7C,
    UNIT_FIELD_ATTACK_POWER_MULTIPLIER = 0x7D,
    UNIT_FIELD_RANGED_ATTACK_POWER = 0x7E,
    UNIT_FIELD_RANGED_ATTACK_POWER_MODS = 0x7F,
    UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER = 0x80,
    UNIT_FIELD_MINRANGEDDAMAGE = 0x81,
    UNIT_FIELD_MAXRANGEDDAMAGE = 0x82,
    UNIT_FIELD_POWER_COST_MODIFIER = 0x83,
    UNIT_FIELD_POWER_COST_MULTIPLIER = 0x8A,
    UNIT_FIELD_MAXHEALTHMODIFIER = 0x91,
    UNIT_FIELD_HOVERHEIGHT = 0x92,
    UNIT_FIELD_PADDING = 0x93,
};

enum class ePlayerFields : uint32_t
{
    PLAYER_DUEL_ARBITER = 0x94,
    PLAYER_FLAGS = 0x96,
    PLAYER_GUILDID = 0x97,
    PLAYER_GUILDRANK = 0x98,
    PLAYER_BYTES = 0x99,
    PLAYER_BYTES_2 = 0x9A,
    PLAYER_BYTES_3 = 0x9B,
    PLAYER_DUEL_TEAM = 0x9C,
    PLAYER_GUILD_TIMESTAMP = 0x9D,
    PLAYER_QUEST_LOG_1_1 = 0x9E,
    PLAYER_QUEST_LOG_1_2 = 0x9F,
    PLAYER_QUEST_LOG_1_3 = 0xA0,
    PLAYER_QUEST_LOG_1_4 = 0xA2,
    PLAYER_QUEST_LOG_2_1 = 0xA3,
    PLAYER_QUEST_LOG_2_2 = 0xA4,
    PLAYER_QUEST_LOG_2_3 = 0xA5,
    PLAYER_QUEST_LOG_2_5 = 0xA7,
    PLAYER_QUEST_LOG_3_1 = 0xA8,
    PLAYER_QUEST_LOG_3_2 = 0xA9,
    PLAYER_QUEST_LOG_3_3 = 0xAA,
    PLAYER_QUEST_LOG_3_5 = 0xAC,
    PLAYER_QUEST_LOG_4_1 = 0xAD,
    PLAYER_QUEST_LOG_4_2 = 0xAE,
    PLAYER_QUEST_LOG_4_3 = 0xAF,
    PLAYER_QUEST_LOG_4_5 = 0xB1,
    PLAYER_QUEST_LOG_5_1 = 0xB2,
    PLAYER_QUEST_LOG_5_2 = 0xB3,
    PLAYER_QUEST_LOG_5_3 = 0xB4,
    PLAYER_QUEST_LOG_5_5 = 0xB6,
    PLAYER_QUEST_LOG_6_1 = 0xB7,
    PLAYER_QUEST_LOG_6_2 = 0xB8,
    PLAYER_QUEST_LOG_6_3 = 0xB9,
    PLAYER_QUEST_LOG_6_5 = 0xBB,
    PLAYER_QUEST_LOG_7_1 = 0xBC,
    PLAYER_QUEST_LOG_7_2 = 0xBD,
    PLAYER_QUEST_LOG_7_3 = 0xBE,
    PLAYER_QUEST_LOG_7_5 = 0xC0,
    PLAYER_QUEST_LOG_8_1 = 0xC1,
    PLAYER_QUEST_LOG_8_2 = 0xC2,
    PLAYER_QUEST_LOG_8_3 = 0xC3,
    PLAYER_QUEST_LOG_8_5 = 0xC5,
    PLAYER_QUEST_LOG_9_1 = 0xC6,
    PLAYER_QUEST_LOG_9_2 = 0xC7,
    PLAYER_QUEST_LOG_9_3 = 0xC8,
    PLAYER_QUEST_LOG_9_5 = 0xCA,
    PLAYER_QUEST_LOG_10_1 = 0xCB,
    PLAYER_QUEST_LOG_10_2 = 0xCC,
    PLAYER_QUEST_LOG_10_3 = 0xCD,
    PLAYER_QUEST_LOG_10_5 = 0xCF,
    PLAYER_QUEST_LOG_11_1 = 0xD0,
    PLAYER_QUEST_LOG_11_2 = 0xD1,
    PLAYER_QUEST_LOG_11_3 = 0xD2,
    PLAYER_QUEST_LOG_11_5 = 0xD4,
    PLAYER_QUEST_LOG_12_1 = 0xD5,
    PLAYER_QUEST_LOG_12_2 = 0xD6,
    PLAYER_QUEST_LOG_12_3 = 0xD7,
    PLAYER_QUEST_LOG_12_5 = 0xD9,
    PLAYER_QUEST_LOG_13_1 = 0xDA,
    PLAYER_QUEST_LOG_13_2 = 0xDB,
    PLAYER_QUEST_LOG_13_3 = 0xDC,
    PLAYER_QUEST_LOG_13_5 = 0xDE,
    PLAYER_QUEST_LOG_14_1 = 0xDF,
    PLAYER_QUEST_LOG_14_2 = 0xE0,
    PLAYER_QUEST_LOG_14_3 = 0xE1,
    PLAYER_QUEST_LOG_14_5 = 0xE3,
    PLAYER_QUEST_LOG_15_1 = 0xE4,
    PLAYER_QUEST_LOG_15_2 = 0xE5,
    PLAYER_QUEST_LOG_15_3 = 0xE6,
    PLAYER_QUEST_LOG_15_5 = 0xE8,
    PLAYER_QUEST_LOG_16_1 = 0xE9,
    PLAYER_QUEST_LOG_16_2 = 0xEA,
    PLAYER_QUEST_LOG_16_3 = 0xEB,
    PLAYER_QUEST_LOG_16_5 = 0xED,
    PLAYER_QUEST_LOG_17_1 = 0xEE,
    PLAYER_QUEST_LOG_17_2 = 0xEF,
    PLAYER_QUEST_LOG_17_3 = 0xF0,
    PLAYER_QUEST_LOG_17_5 = 0xF2,
    PLAYER_QUEST_LOG_18_1 = 0xF3,
    PLAYER_QUEST_LOG_18_2 = 0xF4,
    PLAYER_QUEST_LOG_18_3 = 0xF5,
    PLAYER_QUEST_LOG_18_5 = 0xF7,
    PLAYER_QUEST_LOG_19_1 = 0xF8,
    PLAYER_QUEST_LOG_19_2 = 0xF9,
    PLAYER_QUEST_LOG_19_3 = 0xFA,
    PLAYER_QUEST_LOG_19_5 = 0xFC,
    PLAYER_QUEST_LOG_20_1 = 0xFD,
    PLAYER_QUEST_LOG_20_2 = 0xFE,
    PLAYER_QUEST_LOG_20_3 = 0xFF,
    PLAYER_QUEST_LOG_20_5 = 0x101,
    PLAYER_QUEST_LOG_21_1 = 0x102,
    PLAYER_QUEST_LOG_21_2 = 0x103,
    PLAYER_QUEST_LOG_21_3 = 0x104,
    PLAYER_QUEST_LOG_21_5 = 0x106,
    PLAYER_QUEST_LOG_22_1 = 0x107,
    PLAYER_QUEST_LOG_22_2 = 0x108,
    PLAYER_QUEST_LOG_22_3 = 0x109,
    PLAYER_QUEST_LOG_22_5 = 0x10B,
    PLAYER_QUEST_LOG_23_1 = 0x10C,
    PLAYER_QUEST_LOG_23_2 = 0x10D,
    PLAYER_QUEST_LOG_23_3 = 0x10E,
    PLAYER_QUEST_LOG_23_5 = 0x110,
    PLAYER_QUEST_LOG_24_1 = 0x111,
    PLAYER_QUEST_LOG_24_2 = 0x112,
    PLAYER_QUEST_LOG_24_3 = 0x113,
    PLAYER_QUEST_LOG_24_5 = 0x115,
    PLAYER_QUEST_LOG_25_1 = 0x116,
    PLAYER_QUEST_LOG_25_2 = 0x117,
    PLAYER_QUEST_LOG_25_3 = 0x118,
    PLAYER_QUEST_LOG_25_5 = 0x11A,
    PLAYER_VISIBLE_ITEM_1_ENTRYID = 0x11B,
    PLAYER_VISIBLE_ITEM_1_ENCHANTMENT = 0x11C,
    PLAYER_VISIBLE_ITEM_2_ENTRYID = 0x11D,
    PLAYER_VISIBLE_ITEM_2_ENCHANTMENT = 0x11E,
    PLAYER_VISIBLE_ITEM_3_ENTRYID = 0x11F,
    PLAYER_VISIBLE_ITEM_3_ENCHANTMENT = 0x120,
    PLAYER_VISIBLE_ITEM_4_ENTRYID = 0x121,
    PLAYER_VISIBLE_ITEM_4_ENCHANTMENT = 0x122,
    PLAYER_VISIBLE_ITEM_5_ENTRYID = 0x123,
    PLAYER_VISIBLE_ITEM_5_ENCHANTMENT = 0x124,
    PLAYER_VISIBLE_ITEM_6_ENTRYID = 0x125,
    PLAYER_VISIBLE_ITEM_6_ENCHANTMENT = 0x126,
    PLAYER_VISIBLE_ITEM_7_ENTRYID = 0x127,
    PLAYER_VISIBLE_ITEM_7_ENCHANTMENT = 0x128,
    PLAYER_VISIBLE_ITEM_8_ENTRYID = 0x129,
    PLAYER_VISIBLE_ITEM_8_ENCHANTMENT = 0x12A,
    PLAYER_VISIBLE_ITEM_9_ENTRYID = 0x12B,
    PLAYER_VISIBLE_ITEM_9_ENCHANTMENT = 0x12C,
    PLAYER_VISIBLE_ITEM_10_ENTRYID = 0x12D,
    PLAYER_VISIBLE_ITEM_10_ENCHANTMENT = 0x12E,
    PLAYER_VISIBLE_ITEM_11_ENTRYID = 0x12F,
    PLAYER_VISIBLE_ITEM_11_ENCHANTMENT = 0x130,
    PLAYER_VISIBLE_ITEM_12_ENTRYID = 0x131,
    PLAYER_VISIBLE_ITEM_12_ENCHANTMENT = 0x132,
    PLAYER_VISIBLE_ITEM_13_ENTRYID = 0x133,
    PLAYER_VISIBLE_ITEM_13_ENCHANTMENT = 0x134,
    PLAYER_VISIBLE_ITEM_14_ENTRYID = 0x135,
    PLAYER_VISIBLE_ITEM_14_ENCHANTMENT = 0x136,
    PLAYER_VISIBLE_ITEM_15_ENTRYID = 0x137,
    PLAYER_VISIBLE_ITEM_15_ENCHANTMENT = 0x138,
    PLAYER_VISIBLE_ITEM_16_ENTRYID = 0x139,
    PLAYER_VISIBLE_ITEM_16_ENCHANTMENT = 0x13A,
    PLAYER_VISIBLE_ITEM_17_ENTRYID = 0x13B,
    PLAYER_VISIBLE_ITEM_17_ENCHANTMENT = 0x13C,
    PLAYER_VISIBLE_ITEM_18_ENTRYID = 0x13D,
    PLAYER_VISIBLE_ITEM_18_ENCHANTMENT = 0x13E,
    PLAYER_VISIBLE_ITEM_19_ENTRYID = 0x13F,
    PLAYER_VISIBLE_ITEM_19_ENCHANTMENT = 0x140,
    PLAYER_CHOSEN_TITLE = 0x141,
    PLAYER_FAKE_INEBRIATION = 0x142,
    PLAYER_FIELD_PAD_0 = 0x143,
    PLAYER_FIELD_INV_SLOT_HEAD = 0x144,
    PLAYER_FIELD_PACK_SLOT_1 = 0x172,
    PLAYER_FIELD_BANK_SLOT_1 = 0x192,
    PLAYER_FIELD_BANKBAG_SLOT_1 = 0x1CA,
    PLAYER_FIELD_VENDORBUYBACK_SLOT_1 = 0x1D8,
    PLAYER_FIELD_KEYRING_SLOT_1 = 0x1F0,
    PLAYER_FIELD_CURRENCYTOKEN_SLOT_1 = 0x230,
    PLAYER_FARSIGHT = 0x270,
    PLAYER__FIELD_KNOWN_TITLES = 0x272,
    PLAYER__FIELD_KNOWN_TITLES1 = 0x274,
    PLAYER__FIELD_KNOWN_TITLES2 = 0x276,
    PLAYER_FIELD_KNOWN_CURRENCIES = 0x278,
    PLAYER_XP = 0x27A,
    PLAYER_NEXT_LEVEL_XP = 0x27B,
    PLAYER_SKILL_INFO_1_1 = 0x27C,
    PLAYER_CHARACTER_POINTS1 = 0x3FC,
    PLAYER_CHARACTER_POINTS2 = 0x3FD,
    PLAYER_TRACK_CREATURES = 0x3FE,
    PLAYER_TRACK_RESOURCES = 0x3FF,
    PLAYER_BLOCK_PERCENTAGE = 0x400,
    PLAYER_DODGE_PERCENTAGE = 0x401,
    PLAYER_PARRY_PERCENTAGE = 0x402,
    PLAYER_EXPERTISE = 0x403,
    PLAYER_OFFHAND_EXPERTISE = 0x404,
    PLAYER_CRIT_PERCENTAGE = 0x405,
    PLAYER_RANGED_CRIT_PERCENTAGE = 0x406,
    PLAYER_OFFHAND_CRIT_PERCENTAGE = 0x407,
    PLAYER_SPELL_CRIT_PERCENTAGE1 = 0x408,
    PLAYER_SHIELD_BLOCK = 0x40F,
    PLAYER_SHIELD_BLOCK_CRIT_PERCENTAGE = 0x410,
    PLAYER_EXPLORED_ZONES_1 = 0x411,
    PLAYER_REST_STATE_EXPERIENCE = 0x491,
    PLAYER_FIELD_COINAGE = 0x492,
    PLAYER_FIELD_MOD_DAMAGE_DONE_POS = 0x493,
    PLAYER_FIELD_MOD_DAMAGE_DONE_NEG = 0x49A,
    PLAYER_FIELD_MOD_DAMAGE_DONE_PCT = 0x4A1,
    PLAYER_FIELD_MOD_HEALING_DONE_POS = 0x4A8,
    PLAYER_FIELD_MOD_HEALING_PCT = 0x4A9,
    PLAYER_FIELD_MOD_HEALING_DONE_PCT = 0x4AA,
    PLAYER_FIELD_MOD_TARGET_RESISTANCE = 0x4AB,
    PLAYER_FIELD_MOD_TARGET_PHYSICAL_RESISTANCE = 0x4AC,
    PLAYER_FIELD_BYTES = 0x4AD,
    PLAYER_AMMO_ID = 0x4AE,
    PLAYER_SELF_RES_SPELL = 0x4AF,
    PLAYER_FIELD_PVP_MEDALS = 0x4B0,
    PLAYER_FIELD_BUYBACK_PRICE_1 = 0x4B1,
    PLAYER_FIELD_BUYBACK_TIMESTAMP_1 = 0x4BD,
    PLAYER_FIELD_KILLS = 0x4C9,
    PLAYER_FIELD_TODAY_CONTRIBUTION = 0x4CA,
    PLAYER_FIELD_YESTERDAY_CONTRIBUTION = 0x4CB,
    PLAYER_FIELD_LIFETIME_HONORBALE_KILLS = 0x4CC,
    PLAYER_FIELD_BYTES2 = 0x4CD,
    PLAYER_FIELD_WATCHED_FACTION_INDEX = 0x4CE,
    PLAYER_FIELD_COMBAT_RATING_1 = 0x4CF,
    PLAYER_FIELD_ARENA_TEAM_INFO_1_1 = 0x4E8,
    PLAYER_FIELD_HONOR_CURRENCY = 0x4FD,
    PLAYER_FIELD_ARENA_CURRENCY = 0x4FE,
    PLAYER_FIELD_MAX_LEVEL = 0x4FF,
    PLAYER_FIELD_DAILY_QUESTS_1 = 0x500,
    PLAYER_RUNE_REGEN_1 = 0x519,
    PLAYER_NO_REAGENT_COST_1 = 0x51D,
    PLAYER_FIELD_GLYPH_SLOTS_1 = 0x520,
    PLAYER_FIELD_GLYPHS_1 = 0x526,
    PLAYER_GLYPHS_ENABLED = 0x52C,
    PLAYER_PET_SPELL_POWER = 0x52D,
};


enum TrackObjectFlags
{
    
    Nothing = 0x0,
    Lockpicking = 0x1,
    Herbs = 0x2,
    Minerals = 0x4,
    DisarmTrap = 0x8,
    Open = 0x10,
    Treasure = 0x20,
    CalcifiedElvenGems = 0x40,
    Close = 0x80,
    ArmTrap = 0x100,
    QuickOpen = 0x200,
    QuickClose = 0x400,
    OpenTinkering = 0x800,
    OpenKneeling = 0x1000,
    OpenAttacking = 0x2000,
    Gahzridian = 0x4000,
    Blasting = 0x8000,
    PvPOpen = 0x10000,
    PvPClose = 0x20000,
    Fishing = 0x40000,
    Inscription = 0x80000,
    OpenFromVehicle = 0x100000,
};

enum TrackCreatureFlags
{
    
    
    Beasts = 0x01,
    Dragons = 0x02,
    Demons = 0x04,
    Elementals = 0x08,
    Giants = 0x10,
    Undead = 0x20,
    Humanoids = 0x40,
    Critters = 0x80,
    Machines = 0x100,
    Slimes = 0x200,
    Totem = 0x400,
    NonCombatPet = 0x800,
    GasCloud = 0x1000,
};

std::string get_playername()
{
    auto read = read_memory(0x00C79D18, 13); 
    return read;
}

std::string GetMobNameFromBase(const uint objBase)
{
    uint objName = read_memory<uint>(objBase + 0x964);
    objName = read_memory<uint>(objName + 0x05C);
    return read_memory(objName, 24);
}

struct namecache_ent
{
public:
    ulong plr_guid;
    std::string name;
};

boost::circular_buffer<namecache_ent> namecache(300);

std::string GetPlayerNameFromGuid(const ulong guid)
{
    for (const auto &entry: namecache)
    {
        if (entry.plr_guid == guid)
        {
            return entry.name;
        }
    }

    uint playerMask = read_memory<uint>((nameStore + nameMask));
    uint playerBase1 = read_memory<uint>((nameStore + nameBase));

    
    uint shortGUID = static_cast<uint>(guid) & 0xfffffff;
    uint offset = 12 * (playerMask & shortGUID);

    uint current = read_memory<uint>(playerBase1 + offset + 8);
    offset = read_memory<uint>(playerBase1 + offset);

    
    if ((current & 0x1) == 0x1)
    {
        return "";
    }

    uint testGUID = read_memory<uint>(current);

    while (testGUID != shortGUID)
    {
        current = read_memory<uint>(current + offset + 4);

        
        if ((current & 0x1) == 0x1)
        {
            return "";
        }
        testGUID = read_memory<uint>(current);
    }

    auto name = read_memory(current + nameString, 12);
    namecache.push_back(namecache_ent{guid, name});
    return name;
}

std::string getGameObjectorPlayerName(const GameObject &targetGO)
{
    std::string objectname;

    if (targetGO.type == Player)
    {
        objectname = GetPlayerNameFromGuid(targetGO.guid); 
    }
    else if (targetGO.type == Unit)
    {
        objectname = GetMobNameFromBase(targetGO.address);
    }
    else
    {
        uint pName = read_memory<uint>(targetGO.address + 0x1A4);
        uint pStr = read_memory<uint>(pName + 0x90);

        objectname = read_memory(pStr, 40);
    }
    return objectname;
}

void SetObjectScale(uint32_t descriptorBase, float scale)
{
    
    float currentScale = read_memory<float>(descriptorBase + OBJECT_FIELD_SCALE);

    if (currentScale != scale)
    {
        write_memory<float>(descriptorBase + OBJECT_FIELD_SCALE, scale);
    }
}

bool HasGOFlag(uint32_t descriptorBase, int flag)
{
    int flags = read_memory<int>(descriptorBase + GAMEOBJECT_FLAGS);
    return (flags & flag) != 0;
}


void RemoveGOFlag(uint32_t descriptorBase, int flag)
{
    uint32_t flagAddr = descriptorBase + GAMEOBJECT_FLAGS;
    int flags = read_memory<int>(flagAddr);

    if (flags & flag) 
    {
        flags &= ~flag; 
        write_memory<int>(flagAddr, flags);
    }
}


void AddGOFlag(uint32_t descriptorBase, int flag)
{
    uint32_t flagAddr = descriptorBase + GAMEOBJECT_FLAGS;
    int flags = read_memory<int>(flagAddr);

    if (!(flags & flag))
    {
        flags |= flag;
        write_memory<int>(flagAddr, flags);
    }
}

float RawToFloat(uint32_t raw)
{
    float f;
    std::memcpy(&f, &raw, sizeof(float));
    return f;
}

std::string GetRaceName(uint8_t id)
{
    switch (id)
    {
        case 1: return "Human";
        case 2: return "Orc";
        case 3: return "Dwarf";
        case 4: return "Night Elf";
        case 5: return "Undead";
        case 6: return "Tauren";
        case 7: return "Gnome";
        case 8: return "Troll";
        case 10: return "Blood Elf";
        case 11: return "Draenei";
        default: return "Unknown(" + std::to_string(id) + ")";
    }
}

std::string GetClassName(uint8_t id)
{
    switch (id)
    {
        case 1: return "Warrior";
        case 2: return "Paladin";
        case 3: return "Hunter";
        case 4: return "Rogue";
        case 5: return "Priest";
        case 6: return "Death Knight";
        case 7: return "Shaman";
        case 8: return "Mage";
        case 9: return "Warlock";
        case 11: return "Druid";
        default: return "Unknown(" + std::to_string(id) + ")";
    }
}

std::string GetPowerType(uint8_t id)
{
    switch (id)
    {
        case 0: return "Mana";
        case 1: return "Rage";
        case 2: return "Focus";
        case 3: return "Energy";
        case 6: return "Runic Power";
        default: return "Power(" + std::to_string(id) + ")";
    }
}





std::string ExpandUnitFlags(uint32_t f)
{
    if (f == 0) return "None";
    std::vector<std::string> s;
    if (f & 0x00000002) s.push_back("NonAttackable");
    if (f & 0x00000004) s.push_back("ClientControlLost");
    if (f & 0x00000008) s.push_back("PlayerControlled");
    if (f & 0x00000010) s.push_back("Rename");
    if (f & 0x00000020) s.push_back("Preparation");
    if (f & 0x00000080) s.push_back("NotAttackable_2");
    if (f & 0x00000800) s.push_back("Looting");
    if (f & 0x00001000) s.push_back("PetInCombat");
    if (f & 0x00002000) s.push_back("PvP");
    if (f & 0x00004000) s.push_back("Silenced");
    if (f & 0x00040000) s.push_back("Stunned");
    if (f & 0x00080000) s.push_back("Combat");
    if (f & 0x00100000) s.push_back("TaxiFlight");
    if (f & 0x00200000) s.push_back("Disarmed");
    if (f & 0x00400000) s.push_back("Confused");
    if (f & 0x00800000) s.push_back("Fleeing");
    if (f & 0x02000000) s.push_back("NotSelectable");
    if (f & 0x04000000) s.push_back("Skinnable");
    if (f & 0x08000000) s.push_back("Mount");
    if (s.empty()) return "UnknownBits";

    std::string res;
    for (size_t i = 0; i < s.size(); ++i) res += (i == 0 ? "" : " | ") + s[i];
    return res;
}

std::string ExpandNPCFlags(uint32_t f)
{
    if (f == 0) return "None";
    std::vector<std::string> s;
    if (f & 0x00000001) s.push_back("Gossip");
    if (f & 0x00000002) s.push_back("QuestGiver");
    if (f & 0x00000010) s.push_back("Trainer");
    if (f & 0x00000020) s.push_back("ClassTrainer");
    if (f & 0x00000040) s.push_back("ProfessionTrainer");
    if (f & 0x00000080) s.push_back("Vendor");
    if (f & 0x00000100) s.push_back("VendorAmmo");
    if (f & 0x00000200) s.push_back("VendorFood");
    if (f & 0x00000400) s.push_back("VendorPoison");
    if (f & 0x00000800) s.push_back("VendorReagent");
    if (f & 0x00001000) s.push_back("Repair");
    if (f & 0x00002000) s.push_back("FlightMaster");
    if (f & 0x00004000) s.push_back("SpiritHealer");
    if (f & 0x00008000) s.push_back("SpiritGuide");
    if (f & 0x00010000) s.push_back("Innkeeper");
    if (f & 0x00020000) s.push_back("Banker");
    if (f & 0x00040000) s.push_back("Petitioner");
    if (f & 0x00080000) s.push_back("TabardDesigner");
    if (f & 0x00100000) s.push_back("Battlemaster");
    if (f & 0x00200000) s.push_back("Auctioneer");
    if (f & 0x00400000) s.push_back("StableMaster");
    if (f & 0x00800000) s.push_back("GuildBanker");

    std::string res;
    for (size_t i = 0; i < s.size(); ++i) res += (i == 0 ? "" : " | ") + s[i];
    return res;
}

std::string ExpandGOFlags(uint32_t f)
{
    if (f == 0) return "None";
    std::vector<std::string> s;
    if (f & 1) s.push_back("InUse");
    if (f & 2) s.push_back("Locked");
    if (f & 4) s.push_back("InteractCond");
    if (f & 8) s.push_back("Transport");
    if (f & 16) s.push_back("NotSelectable");
    if (f & 32) s.push_back("NodeSpawn");
    if (f & 64) s.push_back("Triggered");

    std::string res;
    for (size_t i = 0; i < s.size(); ++i) res += (i == 0 ? "" : " | ") + s[i];
    return res;
}

void DumpGameObject(uint32_t address, uint32_t descBase)
{
    if (address == 0 || descBase == 0) return;

    
    uint32_t val_GuidLow = read_memory<uint32_t>(descBase + (uint32_t) WoWObjectFields::GUID * 4);
    uint32_t val_Entry = read_memory<uint32_t>(descBase + (uint32_t) WoWObjectFields::ENTRY * 4);
    uint32_t val_DisplayID = read_memory<uint32_t>(descBase + (uint32_t) WoWGameObjectFields::DISPLAYID * 4);
    uint32_t val_CreatedBy = read_memory<uint32_t>(descBase + (uint32_t) WoWGameObjectFields::CREATED_BY * 4);
    uint32_t val_Scale = read_memory<uint32_t>(descBase + (uint32_t) WoWObjectFields::SCALE_X * 4);

    
    float val_RotX = RawToFloat(read_memory<uint32_t>(descBase + (uint32_t) WoWGameObjectFields::PARENTROTATION * 4));
    float val_RotY = RawToFloat(
        read_memory<uint32_t>(descBase + ((uint32_t) WoWGameObjectFields::PARENTROTATION + 1) * 4));
    float val_RotZ = RawToFloat(
        read_memory<uint32_t>(descBase + ((uint32_t) WoWGameObjectFields::PARENTROTATION + 2) * 4));
    float val_RotW = RawToFloat(
        read_memory<uint32_t>(descBase + ((uint32_t) WoWGameObjectFields::PARENTROTATION + 3) * 4));

    
    uint32_t val_Flags = read_memory<uint32_t>(descBase + (uint32_t) WoWGameObjectFields::FLAGS * 4);
    uint32_t val_Faction = read_memory<uint32_t>(descBase + (uint32_t) WoWGameObjectFields::FACTION * 4);
    uint32_t val_Level = read_memory<uint32_t>(descBase + (uint32_t) WoWGameObjectFields::LEVEL * 4);

    
    uint32_t val_Bytes1 = read_memory<uint32_t>(descBase + (uint32_t) WoWGameObjectFields::BYTES_1 * 4);
    uint8_t b_State = (val_Bytes1 >> 0) & 0xFF;
    uint8_t b_Type = (val_Bytes1 >> 8) & 0xFF;
    uint8_t b_Art = (val_Bytes1 >> 16) & 0xFF;
    uint8_t b_Anim = (val_Bytes1 >> 24) & 0xFF;

    
    uint pName = read_memory<uint>(address + 0x1A4);
    uint pStr = read_memory<uint>(pName + 0x90);
    std::string str_Name = read_memory(pStr, 50);

    
    std::string str_Type;
    switch (b_Type)
    {
        case GO_TYPE_DOOR: str_Type = "Door";
            break;
        case GO_TYPE_BUTTON: str_Type = "Button";
            break;
        case GO_TYPE_QUESTGIVER: str_Type = "QuestGiver";
            break;
        case GO_TYPE_CHEST: str_Type = "Chest (Lootable)";
            break;
        case GO_TYPE_BINDER: str_Type = "Binder";
            break;
        case GO_TYPE_GENERIC: str_Type = "Generic";
            break;
        case GO_TYPE_TRAP: str_Type = "Trap";
            break;
        case GO_TYPE_CHAIR: str_Type = "Chair";
            break;
        case GO_TYPE_SPELL_FOCUS: str_Type = "SpellFocus";
            break;
        case GO_TYPE_TEXT: str_Type = "Text";
            break;
        case GO_TYPE_GOOBER: str_Type = "Goober";
            break;
        case GO_TYPE_TRANSPORT: str_Type = "Transport";
            break;
        case GO_TYPE_CAMERA: str_Type = "Camera";
            break;
        case GO_TYPE_FISHINGNODE: str_Type = "FishingNode";
            break;
        case GO_TYPE_MAILBOX: str_Type = "Mailbox";
            break;
        default: str_Type = "ID_" + std::to_string(b_Type);
            break;
    }

    
    std::string str_FlagExp = ExpandGOFlags(val_Flags);

    fmt::print(
        "================================================================================\n" " GAMEOBJECT DUMP\n"
        "================================================================================\n" " Identity:\n"
        "   Name:            {}\n" "   Entry ID:        {} (0x{:X})\n" "   GUID Low:        {}\n"
        "   Display ID:      {}\n" "   Scale:           {:.2f}\n" "   Created By:      0x{:X}\n" "\n"
        " Classification:\n" "   Type:            [{}] {}\n" "   Faction Tmpl:    {}\n" "   Level:           {}\n"
        "   State:           {} ({})\n" "   ArtKit:          {}\n" "   AnimProg:        {}\n" "\n" " Flags:\n"
        "   Raw:             0x{:08X}\n" "   Expanded:        {}\n" "\n" " Orientation (Quaternion):\n"
        "   X: {:.3f}, Y: {:.3f}, Z: {:.3f}, W: {:.3f}\n"
        "================================================================================\n\n", str_Name, val_Entry,
        val_Entry, val_GuidLow, val_DisplayID, RawToFloat(val_Scale), val_CreatedBy, b_Type, str_Type, val_Faction,
        val_Level, b_State, (b_State == 0 ? "Ready" : "Active"), b_Art, b_Anim, val_Flags, str_FlagExp, val_RotX,
        val_RotY, val_RotZ, val_RotW);
}

void DumpUnit(uint32_t address, uint32_t descBase)
{
    if (address == 0 || descBase == 0) return;

    
    
    

    
    uint32_t val_GuidLow = read_memory<uint32_t>(descBase + (uint32_t) WoWObjectFields::GUID * 4);
    uint32_t val_Entry = read_memory<uint32_t>(descBase + (uint32_t) WoWObjectFields::ENTRY * 4);
    std::string str_Name = GetMobNameFromBase(address);

    
    uint32_t val_DisplayID = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_DISPLAYID * 4);
    uint32_t val_NativeDisplay = read_memory<uint32_t>(
        descBase + (uint32_t) eUnitFields::UNIT_FIELD_NATIVEDISPLAYID * 4);
    uint32_t val_MountDisplay = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_MOUNTDISPLAYID * 4);

    
    uint32_t val_Bytes0 = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_BYTES_0 * 4);
    uint8_t raceId = (val_Bytes0 >> 0) & 0xFF;
    uint8_t classId = (val_Bytes0 >> 8) & 0xFF;
    uint8_t gender = (val_Bytes0 >> 16) & 0xFF;
    uint8_t powerId = (val_Bytes0 >> 24) & 0xFF; 

    
    uint32_t val_Level = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_LEVEL * 4);
    uint32_t val_HpCur = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_HEALTH * 4);
    uint32_t val_HpMax = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_MAXHEALTH * 4);

    
    
    uint32_t powerOffset = (uint32_t) eUnitFields::UNIT_FIELD_POWER1 + powerId;
    uint32_t maxPowerOffset = (uint32_t) eUnitFields::UNIT_FIELD_MAXPOWER1 + powerId;
    uint32_t val_PowCur = read_memory<uint32_t>(descBase + powerOffset * 4);
    uint32_t val_PowMax = read_memory<uint32_t>(descBase + maxPowerOffset * 4);

    
    uint32_t val_Faction = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_FACTIONTEMPLATE * 4);
    uint32_t val_CreatedBy = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_CREATEDBY * 4);
    uint32_t val_SummonedBy = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_SUMMONEDBY * 4);
    uint32_t val_Target = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_TARGET * 4);
    uint32_t val_ChannelObj = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_CHANNEL_OBJECT * 4);

    
    uint32_t val_UnitFlags = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_FLAGS * 4);
    uint32_t val_UnitFlags2 = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_FLAGS_2 * 4);
    uint32_t val_DynFlags = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_DYNAMIC_FLAGS * 4);
    uint32_t val_NpcFlags = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_NPC_FLAGS * 4);

    
    uint32_t val_Bytes1 = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_BYTES_1 * 4);
    uint8_t val_StandState = (val_Bytes1) & 0xFF;

    
    uint32_t val_AttackPower = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_ATTACK_POWER * 4);
    uint32_t val_RangedAttackPower = read_memory<uint32_t>(
        descBase + (uint32_t) eUnitFields::UNIT_FIELD_RANGED_ATTACK_POWER * 4);
    float val_MinDmg = RawToFloat(read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_MINDAMAGE * 4));
    float val_MaxDmg = RawToFloat(read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_MAXDAMAGE * 4));

    
    std::string str_Race = GetRaceName(raceId);
    std::string str_Class = GetClassName(classId);
    std::string str_PowerType = GetPowerType(powerId);
    std::string str_UnitFlags = ExpandUnitFlags(val_UnitFlags);
    std::string str_NpcFlags = ExpandNPCFlags(val_NpcFlags);

    
    std::string str_DynFlags;
    if (val_DynFlags & 1) str_DynFlags += "Lootable ";
    if (val_DynFlags & 2) str_DynFlags += "TrackUnit ";
    if (val_DynFlags & 4) str_DynFlags += "Tagged(Gray) ";
    if (val_DynFlags & 8) str_DynFlags += "TaggedByMe ";
    if (val_DynFlags & 16) str_DynFlags += "SpecialInfo ";
    if (val_DynFlags & 32) str_DynFlags += "Dead ";
    if (val_DynFlags & 64) str_DynFlags += "ReferAFriend ";
    if (val_DynFlags & 128) str_DynFlags += "IsTappedByPlayer ";

    
    
    

    fmt::print(
        "================================================================================\n"
        " UNIT DUMP (NPC/Creature)\n"
        "================================================================================\n" " Identity:\n"
        "   Name:            {}\n" "   Entry:           {}\n" "   GUID Low:        {}\n"
        "   Target GUID:     0x{:08X}\n" "   Created By:      0x{:08X}\n" "   Summoned By:     0x{:08X}\n" "\n"
        " Appearance:\n" "   Race:            {} (ID: {})\n" "   Class:           {} (ID: {})\n"
        "   Gender:          {}\n" "   DisplayID:       {} (Native: {}, Mount: {})\n" "\n" " Vitals:\n"
        "   Level:           {}\n" "   Faction:         {}\n" "   Health:          {} / {} ({:.1f}%)\n"
        "   Power ({}):    {} / {}\n" "   Stand State:     {}\n" "\n" " Combat:\n"
        "   Attack Power:    {} (Ranged: {})\n" "   Damage Range:    {:.1f} - {:.1f}\n" "\n" " Flags:\n"
        "   Unit Flags:      0x{:08X}\n" "     -> {}\n" "   NPC Flags:       0x{:08X}\n" "     -> {}\n"
        "   Dynamic Flags:   0x{:08X}\n" "     -> {}\n" "   Flags_2:         0x{:08X}\n"
        "================================================================================\n\n", str_Name, val_Entry,
        val_GuidLow, val_Target, val_CreatedBy, val_SummonedBy, str_Race, raceId, str_Class, classId,
        (gender == 0 ? "Male" : "Female"), val_DisplayID, val_NativeDisplay, val_MountDisplay, val_Level, val_Faction,
        val_HpCur, val_HpMax, (val_HpMax > 0 ? (float) val_HpCur / val_HpMax * 100.f : 0.f), str_PowerType, val_PowCur,
        val_PowMax, val_StandState, val_AttackPower, val_RangedAttackPower, val_MinDmg, val_MaxDmg, val_UnitFlags,
        str_UnitFlags, val_NpcFlags, str_NpcFlags, val_DynFlags, str_DynFlags, val_UnitFlags2);
}

void DumpPlayer(uint32_t address, uint32_t descBase)
{
    if (address == 0 || descBase == 0) return;

    uint32_t val_GuidLow = read_memory<uint32_t>(descBase + (uint32_t) WoWObjectFields::GUID * 4);
    std::string str_Name = GetPlayerNameFromGuid(val_GuidLow);

    uint32_t val_Bytes0 = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_BYTES_0 * 4);
    uint8_t raceId = (val_Bytes0) & 0xFF;
    uint8_t classId = (val_Bytes0 >> 8) & 0xFF;

    uint32_t val_HpCur = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_HEALTH * 4);
    uint32_t val_HpMax = read_memory<uint32_t>(descBase + (uint32_t) eUnitFields::UNIT_FIELD_MAXHEALTH * 4);

    
    uint32_t val_XP = read_memory<uint32_t>(descBase + (uint32_t) ePlayerFields::PLAYER_XP * 4);
    uint32_t val_NextXP = read_memory<uint32_t>(descBase + (uint32_t) ePlayerFields::PLAYER_NEXT_LEVEL_XP * 4);
    uint32_t val_Coinage = read_memory<uint32_t>(descBase + (uint32_t) ePlayerFields::PLAYER_FIELD_COINAGE * 4);

    
    float val_Block = RawToFloat(
        read_memory<uint32_t>(descBase + (uint32_t) ePlayerFields::PLAYER_BLOCK_PERCENTAGE * 4));
    float val_Dodge = RawToFloat(
        read_memory<uint32_t>(descBase + (uint32_t) ePlayerFields::PLAYER_DODGE_PERCENTAGE * 4));
    float val_Parry = RawToFloat(
        read_memory<uint32_t>(descBase + (uint32_t) ePlayerFields::PLAYER_PARRY_PERCENTAGE * 4));
    float val_Crit = RawToFloat(read_memory<uint32_t>(descBase + (uint32_t) ePlayerFields::PLAYER_CRIT_PERCENTAGE * 4));
    uint32_t val_Expertise = read_memory<uint32_t>(descBase + (uint32_t) ePlayerFields::PLAYER_EXPERTISE * 4);

    
    
    int r_Armor = read_memory<uint32_t>(descBase + ((uint32_t) eUnitFields::UNIT_FIELD_RESISTANCES + 0) * 4);
    int r_Holy = read_memory<uint32_t>(descBase + ((uint32_t) eUnitFields::UNIT_FIELD_RESISTANCES + 1) * 4);
    int r_Fire = read_memory<uint32_t>(descBase + ((uint32_t) eUnitFields::UNIT_FIELD_RESISTANCES + 2) * 4);
    int r_Nature = read_memory<uint32_t>(descBase + ((uint32_t) eUnitFields::UNIT_FIELD_RESISTANCES + 3) * 4);
    int r_Frost = read_memory<uint32_t>(descBase + ((uint32_t) eUnitFields::UNIT_FIELD_RESISTANCES + 4) * 4);
    int r_Shadow = read_memory<uint32_t>(descBase + ((uint32_t) eUnitFields::UNIT_FIELD_RESISTANCES + 5) * 4);
    int r_Arcane = read_memory<uint32_t>(descBase + ((uint32_t) eUnitFields::UNIT_FIELD_RESISTANCES + 6) * 4);

    
    uint32_t val_TrackRes = read_memory<uint32_t>(descBase + (uint32_t) ePlayerFields::PLAYER_TRACK_RESOURCES * 4);
    uint32_t val_TrackCre = read_memory<uint32_t>(descBase + (uint32_t) ePlayerFields::PLAYER_TRACK_CREATURES * 4);

    
    
    std::stringstream ss_Equip;
    for (int i = 0; i < 19; i++)
    {
        uint32_t offset = (uint32_t) ePlayerFields::PLAYER_VISIBLE_ITEM_1_ENTRYID + (i * 2);
        uint32_t itemID = read_memory<uint32_t>(descBase + offset * 4);
        uint32_t itemEnchant = read_memory<uint32_t>(descBase + (offset + 1) * 4);

        if (itemID > 0)
        {
            ss_Equip << fmt::format("   Slot {:02}: ItemID {:<6} | EnchantID {}\n", i + 1, itemID, itemEnchant);
        }
    }

    
    std::stringstream ss_Skills;
    int skillCount = 0;
    for (int i = 0; i < 128; i++)
    {
        uint32_t offset = (uint32_t) ePlayerFields::PLAYER_SKILL_INFO_1_1 + (i * 3);
        uint32_t skillID = read_memory<uint32_t>(descBase + offset * 4);
        if (skillID == 0) continue;

        uint32_t rawLvl = read_memory<uint32_t>(descBase + (offset + 1) * 4);
        uint16_t curLvl = rawLvl & 0xFFFF;
        uint16_t maxLvl = (rawLvl >> 16) & 0xFFFF;

        ss_Skills << fmt::format("   ID {:<4}: {:<3} / {:<3}\n", skillID, curLvl, maxLvl);
        skillCount++;
        if (skillCount > 10)
        {
            ss_Skills << "   ... (More)\n";
            break;
        } 
    }

    
    std::stringstream ss_Quests;
    int questCount = 0;
    for (int i = 0; i < 25; i++)
    {
        
        uint32_t offset = 0x9E + (i * 5);
        uint32_t questID = read_memory<uint32_t>(descBase + offset * 4);

        if (questID > 0)
        {
            uint32_t qState = read_memory<uint32_t>(descBase + (offset + 1) * 4);
            
            ss_Quests << fmt::format("   Slot {:02}: QuestID {:<6} [State: {}]\n", i + 1, questID, qState);
            questCount++;
        }
    }
    if (questCount == 0) ss_Quests << "   (Empty)\n";

    
    std::string str_TrackRes;
    if (val_TrackRes & 0x1) str_TrackRes += "Lockpicking ";
    if (val_TrackRes & 0x2) str_TrackRes += "Herbs ";
    if (val_TrackRes & 0x4) str_TrackRes += "Minerals ";
    if (val_TrackRes & 0x20) str_TrackRes += "Treasure ";
    if (val_TrackRes & 0x40000) str_TrackRes += "Fishing ";

    std::string str_TrackCre;
    if (val_TrackCre & 0x01) str_TrackCre += "Beasts ";
    if (val_TrackCre & 0x04) str_TrackCre += "Demons ";
    if (val_TrackCre & 0x20) str_TrackCre += "Undead ";
    if (val_TrackCre & 0x40) str_TrackCre += "Humanoids ";
    if (val_TrackCre & 0x10) str_TrackCre += "Giants ";

    
    uint32_t g = val_Coinage / 10000;
    uint32_t s = (val_Coinage % 10000) / 100;
    uint32_t c = val_Coinage % 100;

    
    
    

    fmt::print(
        "================================================================================\n"
        " PLAYER DUMP (Extended Unit)\n"
        "================================================================================\n" " Identity:\n"
        "   Name:            {}\n" "   Race:            {}\n" "   Class:           {}\n" "   GUID Low:        {}\n"
        "   Health:          {} / {}\n" "\n" " Progression:\n" "   XP:              {} / {} (Next Lvl)\n"
        "   Money:           {}g {}s {}c\n" "\n" " Combat Stats:\n" "   Crit Chance:     {:.2f}%\n"
        "   Dodge:           {:.2f}%\n" "   Parry:           {:.2f}%\n" "   Block:           {:.2f}%\n"
        "   Expertise:       {}\n" "\n" " Resistances:\n" "   Armor: {} | Holy: {} | Fire: {} | Nature: {}\n"
        "   Frost: {} | Shadow: {} | Arcane: {}\n" "\n" " Tracking:\n" "   Resources:       0x{:X} [{}]\n"
        "   Creatures:       0x{:X} [{}]\n" "\n" " Equipment (Visible):\n" "{}" "\n" " Skills (First 10):\n" "{}" "\n"
        " Quest Log:\n" "{}" "================================================================================\n\n",
        str_Name, GetRaceName(raceId), GetClassName(classId), val_GuidLow, val_HpCur, val_HpMax, val_XP, val_NextXP, g,
        s, c, val_Crit, val_Dodge, val_Parry, val_Block, val_Expertise, r_Armor, r_Holy, r_Fire, r_Nature, r_Frost,
        r_Shadow, r_Arcane, val_TrackRes, str_TrackRes, val_TrackCre, str_TrackCre, ss_Equip.str(), ss_Skills.str(),
        ss_Quests.str());
}





void InspectObject(uint32_t address)
{
    return;
    if (address == 0)
    {
        fmt::print("[-] Invalid Address provided to InspectObject\n");
        return;
    }

    uint32_t descBase = read_memory<uint32_t>(address + 0x08);
    if (descBase == 0)
    {
        fmt::print("[-] Descriptor Base is null for object 0x{:X}\n", address);
        return;
    }

    
    auto type = read_memory<WowObjectType>(address + TYPE);

    
    switch (type)
    {
        case Unit: 
            DumpUnit(address, descBase);
            break;
        case Player: 
            DumpPlayer(address, descBase);
            break;
        case normal_GameObject: 
            DumpGameObject(address, descBase);
            break;
        default:
            fmt::print("[-] Unsupported TypeID: {}\n", type);
            break;
    }
}

external_pointer TOTEM_ARRAY_BASE = 0x00BD0B08;


struct TotemState
{
    uint32_t earth; 
    uint32_t fire; 
    uint32_t water; 
    uint32_t air; 
};

TotemState GetTotemGuids()
{
    TotemState result = {0, 0, 0, 0};

    
    
    result.earth = read_memory<uint32_t>(TOTEM_ARRAY_BASE + (0 * 8));

    
    
    result.fire = read_memory<uint32_t>(TOTEM_ARRAY_BASE + (1 * 8));

    
    
    result.water = read_memory<uint32_t>(TOTEM_ARRAY_BASE + (2 * 8));

    
    
    result.air = read_memory<uint32_t>(TOTEM_ARRAY_BASE + (3 * 8));

    return result;
}


enum UnitDynamicFlags
{
    Lootable = 0x1,
    TrackUnit = 0x2,
    TaggedByOther = 0x4,
    TaggedByMe = 0x8,
    SpecialInfo = 0x10,
    Dead = 0x20,
    ReferAFriendLinked = 0x40,
    IsTappedByAllThreatList = 0x80,
};

enum GameObjectFlags
{
    NONE = 0x0,
    GO_FLAG_IN_USE = 0x00000001, 
    GO_FLAG_LOCKED = 0x00000002, 
    GO_FLAG_INTERACT_COND = 0x00000004,
    
    GO_FLAG_TRANSPORT = 0x00000008, 
    GO_FLAG_NOT_SELECTABLE = 0x00000010, 
    GO_FLAG_NODESPAWN = 0x00000020, 
    GO_FLAG_AI_OBSTACLE = 0x00000040,
    
    GO_FLAG_FREEZE_ANIMATION = 0x00000080,
    
    GO_FLAG_DAMAGED = 0x00000200, 
    GO_FLAG_DESTROYED = 0x00000400, 
    GO_FLAG_IGNORE_CURRENT_STATE_FOR_USE_SPELL = 0x00004000,
    
    GO_FLAG_INTERACT_DISTANCE_IGNORES_MODEL = 0x00008000,
    
    GO_FLAG_IGNORE_CURRENT_STATE_FOR_USE_SPELL_EXCEPT_UNLOCKED = 0x00040000,
    
    GO_FLAG_INTERACT_DISTANCE_USES_TEMPLATE_MODEL = 0x00080000,
    
    GO_FLAG_MAP_OBJECT = 0x00100000, 
    GO_FLAG_IN_MULTI_USE = 0x00200000, 
    GO_FLAG_LOW_PRIORITY_SELECTION = 0x04000000,
    
};

const auto &getUnitDynamicFlagsMap()
{
    static const std::vector<std::pair<UnitDynamicFlags, std::string_view> > map = {
        {UnitDynamicFlags::Lootable, "Lootable"}, {UnitDynamicFlags::TrackUnit, "TrackUnit"},
        {UnitDynamicFlags::TaggedByOther, "TaggedByOther"}, {UnitDynamicFlags::TaggedByMe, "TaggedByMe"},
        {UnitDynamicFlags::SpecialInfo, "SpecialInfo"}, 
        {UnitDynamicFlags::Dead, "Dead"}, {UnitDynamicFlags::ReferAFriendLinked, "ReferAFriendLinked"},
        {UnitDynamicFlags::IsTappedByAllThreatList, "IsTappedByAllThreatList"},
    };
    return map;
}

enum class GameObjectDynamicLowFlags : uint32_t
{
    NONE = 0x00, 
    GO_DYNFLAG_LO_HIDE_MODEL = 0x02, 
    GO_DYNFLAG_LO_ACTIVATE = 0x04, 
    GO_DYNFLAG_LO_ANIMATE = 0x08, 
    GO_DYNFLAG_LO_DEPLETED = 0x10,
    
    GO_DYNFLAG_LO_SPARKLE = 0x20, 
    GO_DYNFLAG_LO_STOPPED = 0x40, 
    GO_DYNFLAG_LO_NO_INTERACT = 0x80,
    GO_DYNFLAG_LO_INVERTED_MOVEMENT = 0x0100, 
    GO_DYNFLAG_LO_HIGHLIGHT = 0x0200,
    
};

const auto &getGameObjectDynamicLowFlagsMap()
{
    static const std::vector<std::pair<GameObjectDynamicLowFlags, std::string_view> > map = {
        {GameObjectDynamicLowFlags::GO_DYNFLAG_LO_HIDE_MODEL, "GO_DYNFLAG_LO_HIDE_MODEL"},
        {GameObjectDynamicLowFlags::GO_DYNFLAG_LO_ACTIVATE, "GO_DYNFLAG_LO_ACTIVATE"},
        {GameObjectDynamicLowFlags::GO_DYNFLAG_LO_ANIMATE, "GO_DYNFLAG_LO_ANIMATE"},
        {GameObjectDynamicLowFlags::GO_DYNFLAG_LO_DEPLETED, "GO_DYNFLAG_LO_DEPLETED"},
        {GameObjectDynamicLowFlags::GO_DYNFLAG_LO_SPARKLE, "GO_DYNFLAG_LO_SPARKLE"},
        {GameObjectDynamicLowFlags::GO_DYNFLAG_LO_STOPPED, "GO_DYNFLAG_LO_STOPPED"},
        {GameObjectDynamicLowFlags::GO_DYNFLAG_LO_NO_INTERACT, "GO_DYNFLAG_LO_NO_INTERACT"},
        {GameObjectDynamicLowFlags::GO_DYNFLAG_LO_INVERTED_MOVEMENT, "GO_DYNFLAG_LO_INVERTED_MOVEMENT"},
        {GameObjectDynamicLowFlags::GO_DYNFLAG_LO_HIGHLIGHT, "GO_DYNFLAG_LO_HIGHLIGHT"},
    };
    return map;
}

enum UnitFlags : unsigned int
{
    UNIT_FLAG_NONE = 0x00000000,
    UNIT_FLAG_SERVER_CONTROLLED = 0x00000001,
    
    UNIT_FLAG_NON_ATTACKABLE = 0x00000002, 
    UNIT_FLAG_DISABLE_MOVE = 0x00000004,
    UNIT_FLAG_PLAYER_CONTROLLED = 0x00000008, 
    UNIT_FLAG_RENAME = 0x00000010,
    UNIT_FLAG_PREPARATION = 0x00000020, 
    UNIT_FLAG_UNK_6 = 0x00000040,
    UNIT_FLAG_NOT_ATTACKABLE_1 = 0x00000080,
    
    UNIT_FLAG_IMMUNE_TO_PC = 0x00000100,
    
    UNIT_FLAG_IMMUNE_TO_NPC = 0x00000200,
    
    UNIT_FLAG_LOOTING = 0x00000400, 
    UNIT_FLAG_PET_IN_COMBAT = 0x00000800, 
    UNIT_FLAG_PVP = 0x00001000, 
    UNIT_FLAG_SILENCED = 0x00002000, 
    UNIT_FLAG_CANNOT_SWIM = 0x00004000, 
    UNIT_FLAG_SWIMMING = 0x00008000, 
    UNIT_FLAG_NON_ATTACKABLE_2 = 0x00010000,
    
    UNIT_FLAG_PACIFIED = 0x00020000, 
    UNIT_FLAG_STUNNED = 0x00040000, 
    UNIT_FLAG_IN_COMBAT = 0x00080000,
    UNIT_FLAG_TAXI_FLIGHT = 0x00100000,
    
    UNIT_FLAG_DISARMED = 0x00200000,
    
    UNIT_FLAG_CONFUSED = 0x00400000,
    UNIT_FLAG_FLEEING = 0x00800000,
    UNIT_FLAG_POSSESSED = 0x01000000, 
    UNIT_FLAG_NOT_SELECTABLE = 0x02000000,
    UNIT_FLAG_SKINNABLE = 0x04000000,
    UNIT_FLAG_MOUNT = 0x08000000,
    UNIT_FLAG_PREVENT_KNEELING_ANIMATION_LOOTING = 0x10000000,
    UNIT_FLAG_PREVENT_EMOTES_FROM_CHAT_TEXT = 0x20000000,
    
    UNIT_FLAG_SHEATHE = 0x40000000,
    UNIT_FLAG_IMMUNE = 0x80000000, 
};

const auto &getUnitFlagsMap()
{
    static const std::vector<std::pair<UnitFlags, std::string_view> > map = {
        {UnitFlags::UNIT_FLAG_SERVER_CONTROLLED, "SERVER_CONTROLLED"},
        {UnitFlags::UNIT_FLAG_NON_ATTACKABLE, "NON_ATTACKABLE"}, {UnitFlags::UNIT_FLAG_DISABLE_MOVE, "DISABLE_MOVE"},
        {UnitFlags::UNIT_FLAG_PLAYER_CONTROLLED, "PLAYER_CONTROLLED"}, {UnitFlags::UNIT_FLAG_RENAME, "RENAME"},
        {UnitFlags::UNIT_FLAG_PREPARATION, "PREPARATION"}, {UnitFlags::UNIT_FLAG_UNK_6, "UNK_6"},
        {UnitFlags::UNIT_FLAG_NOT_ATTACKABLE_1, "NOT_ATTACKABLE_1"},
        {UnitFlags::UNIT_FLAG_IMMUNE_TO_PC, "IMMUNE_TO_PC"}, {UnitFlags::UNIT_FLAG_IMMUNE_TO_NPC, "IMMUNE_TO_NPC"},
        {UnitFlags::UNIT_FLAG_LOOTING, "LOOTING"}, {UnitFlags::UNIT_FLAG_PET_IN_COMBAT, "PET_IN_COMBAT"},
        {UnitFlags::UNIT_FLAG_PVP, "PVP"}, {UnitFlags::UNIT_FLAG_SILENCED, "SILENCED"},
        {UnitFlags::UNIT_FLAG_CANNOT_SWIM, "CANNOT_SWIM"}, {UnitFlags::UNIT_FLAG_SWIMMING, "SWIMMING"},
        {UnitFlags::UNIT_FLAG_NON_ATTACKABLE_2, "NON_ATTACKABLE_2"}, {UnitFlags::UNIT_FLAG_PACIFIED, "PACIFIED"},
        {UnitFlags::UNIT_FLAG_STUNNED, "STUNNED"}, {UnitFlags::UNIT_FLAG_IN_COMBAT, "IN_COMBAT"},
        {UnitFlags::UNIT_FLAG_TAXI_FLIGHT, "TAXI_FLIGHT"}, {UnitFlags::UNIT_FLAG_DISARMED, "DISARMED"},
        {UnitFlags::UNIT_FLAG_CONFUSED, "CONFUSED"}, {UnitFlags::UNIT_FLAG_FLEEING, "FLEEING"},
        {UnitFlags::UNIT_FLAG_POSSESSED, "POSSESSED"}, {UnitFlags::UNIT_FLAG_NOT_SELECTABLE, "NOT_SELECTABLE"},
        {UnitFlags::UNIT_FLAG_SKINNABLE, "SKINNABLE"}, {UnitFlags::UNIT_FLAG_MOUNT, "MOUNT"},
        {UnitFlags::UNIT_FLAG_PREVENT_KNEELING_ANIMATION_LOOTING, "PREVENT_KNEELING_ANIMATION_LOOTING"},
        {UnitFlags::UNIT_FLAG_PREVENT_EMOTES_FROM_CHAT_TEXT, "PREVENT_EMOTES_FROM_CHAT_TEXT"},
        {UnitFlags::UNIT_FLAG_SHEATHE, "SHEATHE"}, {UnitFlags::UNIT_FLAG_IMMUNE, "IMMUNE"},
    };
    return map;
}

const std::vector<std::pair<int, std::string> > flag_names = {
    {UNIT_FLAG_NONE, "UNIT_FLAG_NONE"}, {UNIT_FLAG_SERVER_CONTROLLED, "UNIT_FLAG_SERVER_CONTROLLED"},
    {UNIT_FLAG_NON_ATTACKABLE, "UNIT_FLAG_NON_ATTACKABLE"}, {UNIT_FLAG_DISABLE_MOVE, "UNIT_FLAG_DISABLE_MOVE"},
    {UNIT_FLAG_PLAYER_CONTROLLED, "UNIT_FLAG_PLAYER_CONTROLLED"}, {UNIT_FLAG_RENAME, "UNIT_FLAG_RENAME"},
    {UNIT_FLAG_PREPARATION, "UNIT_FLAG_PREPARATION"}, {UNIT_FLAG_UNK_6, "UNIT_FLAG_UNK_6"},
    {UNIT_FLAG_NOT_ATTACKABLE_1, "UNIT_FLAG_NOT_ATTACKABLE_1"}, {UNIT_FLAG_IMMUNE_TO_PC, "UNIT_FLAG_IMMUNE_TO_PC"},
    {UNIT_FLAG_IMMUNE_TO_NPC, "UNIT_FLAG_IMMUNE_TO_NPC"}, {UNIT_FLAG_LOOTING, "UNIT_FLAG_LOOTING"},
    {UNIT_FLAG_PET_IN_COMBAT, "UNIT_FLAG_PET_IN_COMBAT"}, {UNIT_FLAG_PVP, "UNIT_FLAG_PVP"},
    {UNIT_FLAG_SILENCED, "UNIT_FLAG_SILENCED"}, {UNIT_FLAG_CANNOT_SWIM, "UNIT_FLAG_CANNOT_SWIM"},
    {UNIT_FLAG_SWIMMING, "UNIT_FLAG_SWIMMING"}, {UNIT_FLAG_NON_ATTACKABLE_2, "UNIT_FLAG_NON_ATTACKABLE_2"},
    {UNIT_FLAG_PACIFIED, "UNIT_FLAG_PACIFIED"}, {UNIT_FLAG_STUNNED, "UNIT_FLAG_STUNNED"},
    {UNIT_FLAG_IN_COMBAT, "UNIT_FLAG_IN_COMBAT"}, {UNIT_FLAG_TAXI_FLIGHT, "UNIT_FLAG_TAXI_FLIGHT"},
    {UNIT_FLAG_DISARMED, "UNIT_FLAG_DISARMED"}, {UNIT_FLAG_CONFUSED, "UNIT_FLAG_CONFUSED"},
    {UNIT_FLAG_FLEEING, "UNIT_FLAG_FLEEING"}, {UNIT_FLAG_POSSESSED, "UNIT_FLAG_POSSESSED"},
    {UNIT_FLAG_NOT_SELECTABLE, "UNIT_FLAG_NOT_SELECTABLE"}, {UNIT_FLAG_SKINNABLE, "UNIT_FLAG_SKINNABLE"},
    {UNIT_FLAG_MOUNT, "UNIT_FLAG_MOUNT"},
    {UNIT_FLAG_PREVENT_KNEELING_ANIMATION_LOOTING, "UNIT_FLAG_PREVENT_KNEELING_ANIMATION_LOOTING"},
    {UNIT_FLAG_PREVENT_EMOTES_FROM_CHAT_TEXT, "UNIT_FLAG_PREVENT_EMOTES_FROM_CHAT_TEXT"},
    {UNIT_FLAG_SHEATHE, "UNIT_FLAG_SHEATHE"}, {UNIT_FLAG_IMMUNE, "UNIT_FLAG_IMMUNE"}
};

enum WowClass
{
    Warrior = 1,
    Paladin = 2,
    Hunter = 3,
    Rogue = 4,
    Priest = 5,
    DeathKnight = 6,
    Shaman = 7,
    Mage = 8,
    Warlock = 9,
    Hero = 10, 
    Druid = 11
};

template<>
struct fmt::formatter<WowClass>
{
    constexpr auto parse(format_parse_context &ctx)
    {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const WowClass &p, FormatContext &ctx) const -> decltype(ctx.out())
    {
        switch (p)
        {
            case Warrior:
                return fmt::format_to(ctx.out(), "Warrior");
            case Paladin:
                return fmt::format_to(ctx.out(), "Paladin");
            case Hunter:
                return fmt::format_to(ctx.out(), "Hunter");
            case Rogue:
                return fmt::format_to(ctx.out(), "Rogue");
            case Priest:
                return fmt::format_to(ctx.out(), "Priest");
            case DeathKnight:
                return fmt::format_to(ctx.out(), "DeathKnight");
            case Shaman:
                return fmt::format_to(ctx.out(), "Shaman");
            case Mage:
                return fmt::format_to(ctx.out(), "Mage");
            case Warlock:
                return fmt::format_to(ctx.out(), "Warlock");
            case Druid:
                return fmt::format_to(ctx.out(), "Druid");
            case Hero:
                return fmt::format_to(ctx.out(), "Hero");
            default:
                return fmt::format_to(ctx.out(), "UnknownClass");
        }
    }
};

[[nodiscard]] inline std::vector<Window> pid2windows(const pid_t &pid, Display *display, const Window &w)
{
    const Atom atomPID = XInternAtom(display, "_NET_WM_PID", True);
    if (atomPID == None)
    {
        throw std::runtime_error("_NET_WM_PID atom not found");
    }

    std::vector<Window> result;

    for (std::vector<Window> q{w}; !q.empty();)
    {
        Window current = q.back();
        q.pop_back();

        Atom type;
        int fmt;
        unsigned long n, rem;
        unsigned char *prop = nullptr;
        if (Success == XGetWindowProperty(display, current, atomPID, 0, 1, False, XA_CARDINAL, &type, &fmt, &n, &rem,
                                          &prop) && prop)
        {
            std::unique_ptr<void, decltype([](void *p) { XFree(p); })> prop_ptr(prop);
            if (n > 0 && pid == *reinterpret_cast<pid_t *>(prop))
            {
                result.push_back(current);
            }
        }

        Window root, parent, *children = nullptr;
        unsigned int num_children;
        if (XQueryTree(display, current, &root, &parent, &children, &num_children) && children)
        {
            std::unique_ptr<void, decltype([](void *p) { XFree(p); })> children_ptr(children);
            q.insert(q.end(), children, children + num_children);
        }
    }
    return result;
}

#include <charconv>
#include <filesystem>
#include <fstream>

inline std::vector<char> readFileIntoVector(const std::filesystem::path &filename)
{
    std::ifstream file(filename, std::ios::binary);

    std::ostringstream ss;
    ss << file.rdbuf();
    const std::string &s = ss.str();
    std::vector<char> vec(s.begin(), s.end());

    
    //std::copy(vec.begin(), vec.end(), std::ostream_iterator<char>(std::cout));
    return vec;
}

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

inline pid_t getPIDLossy(std::string &command)
{
    trim(command);
    toLowercase(command);

    for (const auto &dir: std::filesystem::directory_iterator(std::filesystem::path{"/proc"},
                                                              std::filesystem::directory_options::skip_permission_denied))
    {
        
        std::vector<char> proc_read = readFileIntoVector(std::filesystem::path{dir.path() / "cmdline"});

        std::string proc_info;

        for (const auto &entry: proc_read)
        {
            if (entry == '\0') proc_info.push_back(' ');
            else proc_info.push_back(entry);
        }

        trim(proc_info);
        toLowercase(proc_info);

        if (containsSubstring(proc_info, command))
        {
            
            auto strpath = dir.path().filename().string();
            int returnvalue;
            std::from_chars(strpath.data(), strpath.data() + strpath.size(), returnvalue);
            return returnvalue;
        }
    }

    return 0;
}

inline int getPidByName(const char *task_name)
{
    for (const auto &dir: std::filesystem::directory_iterator(std::filesystem::path{"/proc"},
                                                              std::filesystem::directory_options::skip_permission_denied))
    {
        std::string process_name;
        std::ifstream ifs(std::filesystem::path{dir.path() / "status"}, std::ios::binary);

        if (!ifs.good())
        {
            continue;
        }

        if (ifs.ignore(6) >> process_name)
        {
            
            if (process_name == task_name)
            {
                auto strpath = dir.path().filename().string();
                int returnvalue;
                std::from_chars(strpath.data(), strpath.data() + strpath.size(), returnvalue);
                return returnvalue;
            }
        }
    }
    return 0;
}

inline bool StringContainsIgnoreCase(std::string &source, const std::string &substring)
{
    
    std::transform(source.begin(), source.end(), source.begin(), [](unsigned char c) { return std::tolower(c); });

    
    
    std::string subLower = substring;
    std::transform(subLower.begin(), subLower.end(), subLower.begin(), [](unsigned char c) { return std::tolower(c); });

    return source.find(subLower) != std::string::npos;
}

inline bool StringEqualsIgnoreCase(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](unsigned char a, unsigned char b)
    {
        return std::tolower(a) == std::tolower(b);
    });
}

namespace MacroGen
{
    
    
    std::string Cast(std::string_view spellName)
    {
        return fmt::format("/cast {}", spellName);
    }

    
    
    
    std::string CastMouseover(std::string_view spellName)
    {
        return fmt::format("/cast [@mouseover] {}", spellName);
    }

    
    
    std::string CastSelf(std::string_view spellName)
    {
        return fmt::format("/cast [@player] !{}", spellName);
    }

    
    
    std::string CastFocus(std::string_view spellName)
    {
        return fmt::format("/cast [@focus] {}", spellName);
    }

    
    
    std::string CastMeleeStart(std::string_view spellName)
    {
        return fmt::format("/cast {}", spellName);
    }

    std::string CancelAura(std::string_view auraName)
    {
        return fmt::format("/cancelaura {}", auraName);
    }

    std::string StopCasting()
    {
        return "/stopcasting";
    }

    
    
    std::string CastPetAttack(std::string_view spellName)
    {
        return fmt::format("/petattack\r\n/cast {}", spellName);
    }
}






namespace MacroOffsets
{
    const uint32_t ListContainer = 0x00BEAF6C;

    
    const uint32_t Link_Next = 0x00; 
    const int32_t Relative_Name = 0x14; 
    const int32_t Relative_Body = 0x154; 
}

namespace Offsets
{
    const uint32_t ActionBars = 0x00C1E358;
    const uint32_t CurrentPage = 0x00B4B0A8;

    
    const uint32_t MacroListHead = 0x00BEAF74; 
    const uint32_t MacroListNextOff = 0x00BEAF6C; 

    
    
    const uint32_t Node_ID = 0x18; 
    const uint32_t Node_Name = 0x20; 
    const uint32_t Node_Icon = 0x60; 

    
    const uint32_t Node_Body = 0x160;
    const size_t Node_Body_Size = 255; 
}

uint32_t GetMacroBodyAddress(const std::string_view targetName)
{
    
    uint32_t currentLinkPtr = read_memory<uint32_t>(Offsets::MacroListHead);
    uint32_t nextOffsetModifier = read_memory<uint32_t>(Offsets::MacroListNextOff);

    int safety = 0;
    const int MAX_LOOPS = 200;

    while (currentLinkPtr != 0 && (currentLinkPtr & 1) == 0 && safety < MAX_LOOPS)
    {
        
        std::string name = read_memory(currentLinkPtr + Offsets::Node_Name, 64);

        if (name == targetName)
        {
            return currentLinkPtr + Offsets::Node_Body;
        }

        
        
        uint32_t nextPtrAddr = currentLinkPtr + nextOffsetModifier + 4;
        currentLinkPtr = read_memory<uint32_t>(nextPtrAddr);

        safety++;
    }

    return 0;
}

std::vector<ulong> stringdebug;

void debug_macros()
{
    auto addr = GetMacroBodyAddress("test1");
    std::string body_contents = read_memory(addr, 255);
    auto hash = HashedString::create(body_contents).get_hash();

    for (const auto &entry: stringdebug)
    {
        if (entry == hash)
        {
            return;
        }
    }

    fmt::println("{:x} \"{}\"", addr, body_contents);
    stringdebug.emplace_back(hash);
}

inline bool ReplaceMacroBody(uint32_t bodyAddress, const std::string &newContent1)
{
    if (bodyAddress == 0) return false;

    constexpr size_t MAX_BODY = 254;
    
    auto newContent = fmt::format("{}                                                  ", newContent1);
    if (newContent.length() > MAX_BODY)
    {
        logWarn("ReplaceMacroBody: Content too long ({} > {})", newContent.length(), MAX_BODY);
        return false;
    }

    
    std::string current = read_memory(bodyAddress, 255);
    if (current == newContent) return true;

    
    size_t writeSize = newContent.length() + 1;
    bool success = write_memory(bodyAddress, (void *) newContent.c_str(), writeSize);

    if (!success)
    {
        logWarn("ReplaceMacroBody: write_memory failed!");
        return false;
    }

    
    std::string verify = read_memory(bodyAddress, newContent.length() + 1);
    if (verify != newContent)
    {
        logWarn("ReplaceMacroBody: Verification failed!");
        logWarn("  Expected: '{}' ({} bytes)", newContent, newContent.length());
        logWarn("  Got:      '{}' ({} bytes)", verify, verify.length());

        
        size_t minLen = std::min(newContent.length(), verify.length());
        for (size_t i = 0; i < minLen; i++)
        {
            if (newContent[i] != verify[i])
            {
                logWarn("  First mismatch at byte {}: expected 0x{:02X}, got 0x{:02X}", i, (uint8_t) newContent[i],
                        (uint8_t) verify[i]);
                break;
            }
        }
        return false;
    }

    return true;
}

inline std::string GetMacroNameByID(uint32_t targetID)
{
    uint32_t currentNode = read_memory<uint32_t>(Offsets::MacroListHead);
    uint32_t nextOffsetModifier = read_memory<uint32_t>(Offsets::MacroListNextOff);
    int safety = 0;

    while (currentNode != 0 && (currentNode & 1) == 0 && safety++ < 200)
    {
        uint32_t id = read_memory<uint32_t>(currentNode + Offsets::Node_ID);

        if (id == targetID)
        {
            return read_memory(currentNode + Offsets::Node_Name, 64);
        }

        uint32_t nextPtrAddr = currentNode + nextOffsetModifier + 4;
        currentNode = read_memory<uint32_t>(nextPtrAddr);
    }
    return "Unknown Macro";
}

inline void SetFirstSlotToMacro(const std::string &macroName)
{
    uint32_t macroID = 0;
    {
        uint32_t node = read_memory<uint32_t>(Offsets::MacroListHead);
        uint32_t mod = read_memory<uint32_t>(Offsets::MacroListNextOff);
        int safety = 0;

        while (node != 0 && (node & 1) == 0 && safety++ < 500)
        {
            std::string name = read_memory(node + Offsets::Node_Name, 20);
            if (name == macroName)
            {
                macroID = read_memory<uint32_t>(node + Offsets::Node_ID);
                break;
            }
            node = read_memory<uint32_t>(node + mod + 4);
        }
    }

    if (macroID == 0)
    {
        fmt::println("Macro requested \"{}\" was not found in macro bucket", macroName);
        return;
    }
    uint32_t actionValue = 0x40000000 | macroID;

    uint32_t targetAddress = selected_castbar_addr;

    if (read_memory<uint32_t>(targetAddress) == actionValue)
    {
        
        return;
    }

    write_memory<uint32_t>(targetAddress, actionValue);
}

bool doesGameObjectExist(const ulong targetguid)
{
    if (targetguid <= 0) return false;
    return std::any_of(gameobjects_seen.begin(), gameobjects_seen.end(), [&targetguid](const auto &entry)
    {
        return entry.guid == targetguid;
    });
}

uint playerBase = 0;
ulong resolved_player_guid = 0;
WowClass resolved_player_class;

bool findGameObjectByGUID(const ulong targetguid, GameObject &input, const WowObjectType type_specific = notype)
{
    if (targetguid <= 0) return false;
    if (targetguid == resolved_player_guid)
    {
        return false;
    }

    auto it = std::find_if(gameobjects_seen.begin(), gameobjects_seen.end(),
                           [&targetguid, &type_specific](const auto &entry)
                           {
                               return entry.guid == targetguid && (
                                          type_specific == notype || entry.type == type_specific);
                           });

    if (it != gameobjects_seen.end())
    {
        input = *it;
        return true;
    }

    return false;
}


bool findGameobjectByName(const std::string &name, GameObject &input, const bool full = false)
{
    for (const auto &entry: gameobjects_seen)
    {
        uint pName = read_memory<uint>(entry.address + 0x1A4);
        uint pStr = read_memory<uint>(pName + 0x90);

        auto objectName = read_memory(pStr, 40);

        if (objectName.empty())
        {
            continue;
        }

        

        if (objectName == name)
        {
            input.guid = entry.guid;
            input.address = entry.address;

            if (full)
            {
                input.position = Point{
                    read_memory<float>(entry.address + POS_X), read_memory<float>(entry.address + POS_Y),
                    read_memory<float>(entry.address + POS_Z)
                };
            }

            return true;
        }
    }

    return false;
}

uint32_t current_macro_address;
