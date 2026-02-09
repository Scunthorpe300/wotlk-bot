#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <cstdio>
#include <GLFW/glfw3.h>
#include <SFML/Audio.hpp>
#include <cmath>
#include "ArrayDatabase.hpp"
#include <X11/Xlib.h>
#include "fmt/format.h"
#include "fmt/printf.h"
#include <X11/XKBlib.h>
#include <random>
#include <sys/uio.h>
#include <boost/circular_buffer.hpp>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <csignal>
#include <thread>
#include <boost/serialization/vector.hpp>
#include "xdotool/xdo.h"
#include "consolidation_header.hpp"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include "gperftools/malloc_extension.h"
#include "gperftools/tcmalloc.h"
#include "string_hashing.hpp"
#include "fmt/ranges.h"
#include <boost/asio.hpp>
#include <boost/process/v2/process.hpp>
#include "spell_defines.hpp"

#include "sounds.hpp"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"

constexpr int expanded_width = 500;
constexpr int expanded_height = 350;
constexpr int collapsed_width = 150;
constexpr int collapsed_height = 30;

bool tracking_hack_enabled = false; 
bool anti_afk_enabled = false;


enum wotlkbotSettings
{
    random_rotation,
    paladin_tank_rotation,
    paladin_ret_rotation,
    balance_druid_rotation,
    warrior_arms_rotation,
    fishing_mode,
    hunter_rotation,
    healer_mode,
    shaman_dps_rotation,
    mage_dps_rotation,
    warlock_dps_rotation,
    feral_mode,
    deathknight_rotation,
    warrior_fury_rotation,
    deathknight_unholy_rotation,
    rogue_sub_mode,
    warrior_tank,
    onyxia_firemage_rot,
    warrior_armstank,
    rogue_com_mode,
    rogue_ass_mode,
    shadowpriest_mode,
    ascension_elemental,
    asc_hunter_rotation,
    paladin_ret_rotation_ascension,
    paladin_tank_rotation_ascension,
    priest_bronzebeard_and_epoch_dps,
};

wotlkbotSettings settings_for_guid(const ulong player_guid)
{
    if (player_guid <= 0)
    {
        return random_rotation;
    }

    for (const auto &entry: CurrentlyActiveSettings)
    {
        if (entry.character_guid == player_guid)
        {
            return static_cast<wotlkbotSettings>(entry.selected_setting);
        }
    }

    return random_rotation; 
}

void SetSettingForUID(const ulong player_guid, const wotlkbotSettings &new_setting)
{
    for (auto &entry: CurrentlyActiveSettings)
    {
        if (entry.character_guid == player_guid)
        {
            entry.selected_setting = new_setting;
            return;
        }
    }
    CurrentlyActiveSettings.push_back(wotlkbotSettingsDB::Settings{new_setting, player_guid});
}

std::random_device myrandomd;
std::default_random_engine rng(myrandomd());

inline int random(const int min, const int max)
{
    std::uniform_int_distribution<> distr(min, max);
    return distr(rng);
}

struct spell
{
    HashedString name;
    int id;

    spell() = default;

    spell(int spell_id, const std::string &spell_name) : name(HashedString::create(spell_name)), id(spell_id)
    {
    }

    
    bool operator==(const spell &other) const { return name == other.name; };
};

#ifdef DEBUG
namespace fmt
{
    template<>
    struct formatter<spell>
    {
    private:
        
        
        char presentation = 'd';

    public:
        
        
        constexpr auto parse(format_parse_context &ctx)
        {
            auto it = ctx.begin(), end = ctx.end();

            
            if (it != end && *it != '}')
            {
                presentation = *it++;
                
                if (presentation != 'd' && presentation != 'n' && presentation != 'i')
                {
                    throw format_error("invalid format specifier for spell");
                }
            }

            
            if (it != end && *it != '}')
            {
                throw format_error("invalid format specifier");
            }

            return it;
        }

        
        template<typename FormatContext>
        auto format(const spell &s, FormatContext &ctx) const
        {
            
            if (s.name.get_hash() == 0)
            {
                return fmt::format_to(ctx.out(), "(Invalid Spell)");
            }

            
            switch (presentation)
            {
                case 'n': 
                    return fmt::format_to(ctx.out(), "{}", s.name);
                case 'i': 
                    return fmt::format_to(ctx.out(), "{}", s.id);
                default: 
                    return fmt::format_to(ctx.out(), "Spell ID: {} | Name: '{}'", s.id, s.name);
            }
        }
    };
}
#endif

std::unordered_map<int, spell> spell_database;

xdo *x;
Display *dpy;
Screen *screen;
int scr;
Window root_window;

XID target;

std::vector<Window> pid2windows(const pid_t pid, Display *display)
{
    if (pid == 0) return {};

    return pid2windows(pid, display, XDefaultRootWindow(display));
}

std::atomic<bool> shutdown_requested = false;

void target_window()
{
    if (wow_window == 0)
    {
        
        auto res = pid2windows(wow_pid, dpy);

        if (!res.empty())
        {
            wow_window = res.front();
            
        }
    }
}

constexpr const uint32_t ADDR_READ_INDEX = 0x00d41400;
constexpr const uint32_t ADDR_WRITE_INDEX = 0x00d41404;
constexpr const uint32_t ADDR_BUFFER_BASE = 0x00d41408;
constexpr const int BUFFER_CAPACITY = 16;
constexpr const int STRUCT_SIZE = 20; 


constexpr uint32_t get_vk_from_name(const char *keyname)
{
    if (!keyname || std::strlen(keyname) == 0) return 0;
    if (std::strlen(keyname) == 1)
    {
        char c = keyname[0];
        if (c >= 'a' && c <= 'z') c = std::toupper(c);
        return static_cast<uint32_t>(c);
    }
    char *end_ptr;
    unsigned long raw_val = std::strtoul(keyname, &end_ptr, 0);
    if (*end_ptr == '\0' && raw_val > 0 && raw_val < 256) return static_cast<uint32_t>(raw_val);
    if (std::strcmp(keyname, "esc") == 0) return 0x1B;
    return 0;
}


uint32_t GetSafeTimestamp()
{
    int write_idx = read_memory<int>(ADDR_WRITE_INDEX);

    
    
    int safe_idx = (write_idx - 2 + BUFFER_CAPACITY) % BUFFER_CAPACITY;

    uint32_t addr = ADDR_BUFFER_BASE + (safe_idx * STRUCT_SIZE);
    uint32_t last_ts = read_memory<uint32_t>(addr + 0x10);

    
    if (last_ts == 0) return 100000;
    return last_ts;
}

void send_key_queue(const std::vector<const char *> &key_queue)
{
    size_t keys_sent = 0;
    size_t total_keys = key_queue.size();

    
    constexpr const int REQUIRED_SLOTS = 2 + 2;

    int contention_retries = 0;
    const int MAX_RETRIES = 100;

    while (keys_sent < total_keys && contention_retries < MAX_RETRIES)
    {
        
        int read_idx = read_memory<int>(ADDR_READ_INDEX);
        int write_idx_start = read_memory<int>(ADDR_WRITE_INDEX); 

        int used_slots = (write_idx_start - read_idx + BUFFER_CAPACITY) % BUFFER_CAPACITY;
        int free_slots = (BUFFER_CAPACITY - 1) - used_slots;

        
        if (free_slots < REQUIRED_SLOTS)
        {
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        const char *key_name = key_queue[keys_sent];
        uint32_t vk_code = get_vk_from_name(key_name);

        if (vk_code == 0)
        {
            keys_sent++; 
            continue;
        }

        
        uint32_t base_ts = GetSafeTimestamp();
        
        

        int idx_down = write_idx_start;
        int idx_up = (write_idx_start + 1) % BUFFER_CAPACITY;
        int idx_next = (write_idx_start + 2) % BUFFER_CAPACITY;

        uint32_t addr_down = ADDR_BUFFER_BASE + (idx_down * STRUCT_SIZE);
        uint32_t addr_up = ADDR_BUFFER_BASE + (idx_up * STRUCT_SIZE);

        
        
        

        
        write_memory<uint32_t>(addr_down + 0x00, 7);
        write_memory<uint32_t>(addr_down + 0x04, vk_code);
        write_memory<uint32_t>(addr_down + 0x08, 1);
        write_memory<uint32_t>(addr_down + 0x0C, 0);
        write_memory<uint32_t>(addr_down + 0x10, base_ts + 10);

        
        write_memory<uint32_t>(addr_up + 0x00, 8);
        write_memory<uint32_t>(addr_up + 0x04, vk_code);
        write_memory<uint32_t>(addr_up + 0x08, 1);
        write_memory<uint32_t>(addr_up + 0x0C, 0);
        write_memory<uint32_t>(addr_up + 0x10, base_ts + 25); 

        
        
        int check_write_idx = read_memory<int>(ADDR_WRITE_INDEX);

        if (check_write_idx == write_idx_start)
        {
            
            
            write_memory<int>(ADDR_WRITE_INDEX, idx_next);
            keys_sent++;
            contention_retries = 0; 
        }
        else
        {
            
            
            
            

            
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            contention_retries++;

            
        }
    }
}

void send_key(const char *keyout)
{
    if (!keyout) return;
    std::vector<const char *> single_key = {keyout};
    send_key_queue(single_key);
}

void send_mouse()
{
    if (wow_window == 0)
    {
        return;
    }

    xdo_click_window(x, wow_window, 1);
}

void cleanup()
{
    logInfo("cleanup() called");
    shutdown_requested = true;
    if (x)
    {
        xdo_free(x);
        x = nullptr;
    }

    AccountSettings.save(true);
    CurrentlyActiveSettings.save(true);
    tc_malloc_stats();
    MallocExtension::instance()->ReleaseFreeMemory();
}


struct screen_color
{
public:
    int r;
    int g;
    int b;
};

screen_color get_pixel_at(const int xx, const int yy)
{
    XImage *image = XGetImage(dpy, XRootWindow(dpy, XDefaultScreen(dpy)), xx, yy, 1, 1, AllPlanes, ZPixmap);
    if (!image) return screen_color{0, 0, 0};

    XColor c;
    c.pixel = image->f.get_pixel(image, 0, 0);
    image->f.destroy_image(image);

    XQueryColor(dpy, XDefaultColormap(dpy, XDefaultScreen(dpy)), &c);
    return screen_color{c.red / 256, c.green / 256, c.blue / 256};
}

bool should_run_code = false;

struct mouse_pointer
{
public:
    int x;
    int y;
};


mouse_pointer current_mouse_pos()
{
    int root_x, root_y;
    XQueryPointer(dpy, XRootWindow(dpy, scr), nullptr, nullptr, &root_x, &root_y, nullptr, nullptr, nullptr);
    return mouse_pointer{root_x, root_y};
}

void glfw_error_callback(int error, const char *description)
{
    logAll("GLFW Error {}: {}", error, description);
}

GameObject ourself;

WowClass read_class()
{
    auto read = read_memory<std::byte>(0xC79E89);
    auto pc = static_cast<WowClass>(read);
    resolved_player_class = pc;
    return pc;
}

int percent(const int a, const int b)
{
    if (a <= 0 || b <= 0) return 0;
    if (a >= b) return 100;

    double result = static_cast<double>(a) / b * 100.0;
    return static_cast<int>(std::clamp(result, 0.0, 100.0));
}

int manapower()
{
    return read_memory<int>(ourself.descriptor + 25 * 4);
}


int ragepower()
{
    return read_memory<int>(ourself.descriptor + 26 * 4) / 10;
}


short energypower()
{
    
    
    
    return read_memory<short>((ourself.descriptor - 2456));
}

int runicpower()
{
    auto read = read_memory<int>(ourself.address + 0x19D4);
    if (read <= 0)
    {
        return 0;
    }
    return read / 10;
}

int maxmanapower()
{
    return read_memory<int>(ourself.descriptor + 33 * 4);
}

double manapower_percent()
{
    return percent(manapower(), maxmanapower());
}

UnitFlags ReadFlagsForGO(const GameObject &targetGO)
{
    uint32_t pStats_address = read_memory<uint32_t>(targetGO.address + 0xD0);
    uint32_t raw_flags = read_memory<uint32_t>(pStats_address + 0xD4);
    return static_cast<UnitFlags>(raw_flags);
}

UnitDynamicFlags ReadDynFlagsForGO(const GameObject &targetGO)
{
    auto read = read_memory<int>(targetGO.descriptor + 0x13C);
    return static_cast<UnitDynamicFlags>(read);
}

GameObjectDynamicLowFlags ReadDynLowFlagsForGO(const GameObject &targetGO)
{
    auto read = read_memory<uint>(targetGO.descriptor + 0xF0);
    return static_cast<GameObjectDynamicLowFlags>(read);
}

uint32_t GetObjectFieldDataPtr(uint32_t object_ptr)
{
    if (object_ptr == 0) return 0;
    uint32_t pDescriptor = read_memory<uint32_t>(object_ptr + 0x8);
    if (pDescriptor == 0) return 0;

    return pDescriptor;
}

uint32_t GetUnitFlags(uint32_t unit_ptr)
{
    uint32_t pFieldData = GetObjectFieldDataPtr(unit_ptr);
    if (!pFieldData) return 0;

    
    uint32_t address = pFieldData + (static_cast<uint32_t>(eUnitFields::UNIT_FIELD_FLAGS) * 4);

    
    return read_memory<uint32_t>(address);
}

uint32_t GetUnitDynamicFlags(uint32_t unit_ptr)
{
    uint32_t pFieldData = GetObjectFieldDataPtr(unit_ptr);
    if (!pFieldData) return 0;

    uint32_t address = pFieldData + (static_cast<uint32_t>(eUnitFields::UNIT_DYNAMIC_FLAGS) * 4);

    return read_memory<uint32_t>(address);
}

uint32_t GetGameObjectFlags(uint32_t gameobject_ptr)
{
    uint32_t pFieldData = GetObjectFieldDataPtr(gameobject_ptr);
    if (!pFieldData) return 0;

    uint32_t address = pFieldData + (static_cast<uint32_t>(WoWGameObjectFields::FLAGS) * 4);

    return read_memory<uint32_t>(address);
}

uint32_t GetGameObjectDynamicFlags(uint32_t gameobject_ptr)
{
    uint32_t pFieldData = GetObjectFieldDataPtr(gameobject_ptr);
    if (!pFieldData) return 0;

    uint32_t address = pFieldData + (static_cast<uint32_t>(WoWGameObjectFields::DYNAMIC) * 4);

    return read_memory<uint32_t>(address);
}

uint32_t ReadGameObjectDynamicLowFlags(uint32_t gameobject_ptr)
{
    
    uint32_t pFieldData = GetObjectFieldDataPtr(gameobject_ptr);
    if (pFieldData == 0)
    {
        fmt::println("No result from getObjectFieldDataPtr");
        return 0; 
    }

    
    
    
    uint32_t dynamic_flags_address = pFieldData + (static_cast<uint32_t>(WoWGameObjectFields::DYNAMIC) * 4);

    
    return read_memory<uint32_t>(dynamic_flags_address);
}

enum class eCGGameObjectTypeId : uint8_t
{
    Door = 0,
    Button = 1,
    Questgiver = 2,
    Chest = 3,
    Binder = 4,
    Generic = 5,
    Trap = 6,
    Chair = 7,
    SpellFocus = 8,
    Text = 9,
    Goober = 10,
    TransportElevator = 11,
    AreaDamage = 12,
    Camera = 13,
    Mapobject = 14,
    MoTransportShip = 15,
    DuelFlag = 16,
    FishingNode = 17,
    Ritual = 18,
    Mailbox = 19,
    
};

enum class eCGGameObjectState : uint8_t
{
    Active = 0, 
    Ready = 1, 
    ActiveAlternative = 2, 
};

uint32_t ReadGameObjectBytes1(uint32_t gameobject_ptr)
{
    uint32_t pFieldData = GetObjectFieldDataPtr(gameobject_ptr);
    if (!pFieldData) return 0;

    
    uint32_t address = pFieldData + 0x44;
    return read_memory<uint32_t>(address);
}



inline GameObjectDynamicLowFlags operator|(GameObjectDynamicLowFlags a, GameObjectDynamicLowFlags b)
{
    return static_cast<GameObjectDynamicLowFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

bool InCombat()
{
    auto flags = ReadFlagsForGO(ourself);
    return (flags & (UNIT_FLAG_IN_COMBAT | UNIT_FLAG_PET_IN_COMBAT)) != 0;
}

size_t MAX_THREADS = std::min((size_t) 8, (size_t) std::thread::hardware_concurrency());

namespace SpellDBCOffsets
{
    constexpr uint32_t UpperBound = 0x0C;
    constexpr uint32_t LowerBound = 0x10;
    constexpr uint32_t TableBase = 0x20;
    constexpr uint32_t SpellID = 0x00;
    constexpr uint32_t NamePtr = 0x220;
}

#pragma pack(push, 1)
struct SpellRecordPartial
{
    int spellId; 
    
    char _padding[0x220 - sizeof(int)];
    uint32_t namePtr; 
};
#pragma pack(pop)

void CacheSpellsDBC()
{
    if (!spell_database.empty())
    {
        return;
    }

    uint32_t lower = read_memory<uint32_t>(SPELL_DATABASE_CACHE + SpellDBCOffsets::LowerBound);
    uint32_t upper = read_memory<uint32_t>(SPELL_DATABASE_CACHE + SpellDBCOffsets::UpperBound);
    uint32_t tableBase = read_memory<uint32_t>(SPELL_DATABASE_CACHE + SpellDBCOffsets::TableBase);

    logWarn("DBC Range: {:x} to {:x}. Table Base: {:x}", lower, upper, tableBase);

    if (lower > upper) std::swap(lower, upper);
    if (upper == 0 || lower >= upper)
    {
        logWarn("Error: Invalid spell index range detected.");
        return;
    }

    size_t total_indices = upper - lower + 1;

    
    logWarn("Reading pointer table ({} indices) in bulk...", total_indices);
    std::vector<uint32_t> entryPtrs(total_indices);
    const uint32_t table_start_addr = tableBase + lower * sizeof(uint32_t);
    const size_t table_size_bytes = total_indices * sizeof(uint32_t);

    if (!read_memory(table_start_addr, entryPtrs.data(), table_size_bytes))
    {
        logWarn("Error: Failed to bulk read the spell pointer table.");
        return;
    }

    
    std::vector<uint32_t> validEntryPtrs;
    validEntryPtrs.reserve(entryPtrs.size());
    for (uint32_t ptr: entryPtrs)
    {
        if (ptr != 0)
        {
            validEntryPtrs.push_back(ptr);
        }
    }

    if (validEntryPtrs.empty())
    {
        logWarn("No valid spell entries found in the pointer table.");
        return;
    }

    size_t total_spells = validEntryPtrs.size();
    size_t thread_count = std::min(total_spells, MAX_THREADS);
    if (thread_count == 0) return;

    using SpellPair = std::pair<int, spell>;

    auto block_size = static_cast<size_t>(std::ceil(static_cast<double>(total_spells) / thread_count));
    std::vector<std::vector<SpellPair> > collected_data(total_spells);

    logWarn("Using {} threads to process {} valid spell pointers (approx. {} per thread).", thread_count, total_spells,
            block_size);

    auto worker_task = [&](size_t t_id, size_t start_idx, size_t end_idx)
    {
        auto &local_collection = collected_data[t_id];
        local_collection.reserve(end_idx - start_idx);

        SpellRecordPartial record_buffer; 

        for (size_t i = start_idx; i < end_idx; ++i)
        {
            uint32_t entryPtr = validEntryPtrs[i];

            
            if (!read_memory(entryPtr, &record_buffer, sizeof(SpellRecordPartial)))
            {
                continue; 
            }

            
            if (record_buffer.namePtr == 0) continue;

            
            auto name = read_memory(record_buffer.namePtr, 128);
            if (name.empty()) continue;

            local_collection.emplace_back(record_buffer.spellId, spell(record_buffer.spellId, name));
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (size_t t = 0; t < thread_count; ++t)
    {
        size_t start = t * block_size;
        size_t end = std::min(start + block_size, total_spells);
        if (start >= end) continue; 
        workers.emplace_back(worker_task, t, start, end);
    }

    for (auto &worker: workers)
    {
        worker.join();
    }

    
    size_t final_count = 0;
    for (const auto &local_collection: collected_data)
    {
        final_count += local_collection.size();
    }
    spell_database.reserve(final_count);

    for (auto &local_collection: collected_data)
    {
        
        spell_database.insert(std::make_move_iterator(local_collection.begin()),
                              std::make_move_iterator(local_collection.end()));
    }

    logWarn("Spells database populated with {} entries.", spell_database.size());
#ifndef DEBUG
#ifdef HAS_TCMALLOC
    
    collected_data.clear();
    MallocExtension::instance()->ReleaseFreeMemory();
#endif
#endif
}

[[maybe_unused]] void dumpdynflagsforgo(const GameObject &entry)
{
    auto dynflags = ReadDynFlagsForGO(entry);

    fmt::println("Dyn flags comp {}", static_cast<int>(dynflags));
    if (dynflags & Lootable)
    {
        fmt::println("UnitDynamicFlags::Lootable");
    }

    if (dynflags & TrackUnit)
    {
        fmt::println("UnitDynamicFlags::TrackUnit");
    }
    if (dynflags & TaggedByOther)
    {
        fmt::println("UnitDynamicFlags::TaggedByOther");
    }
    if (dynflags & TaggedByMe)
    {
        fmt::println("UnitDynamicFlags::TaggedByMe");
    }
    if (dynflags & SpecialInfo)
    {
        fmt::println("UnitDynamicFlags::SpecialInfo");
    }
    if (dynflags & Dead)
    {
        fmt::println("UnitDynamicFlags::Dead");
    }
    if (dynflags & ReferAFriendLinked)
    {
        fmt::println("UnitDynamicFlags::ReferAFriendLinked");
    }
    if (dynflags & IsTappedByAllThreatList)
    {
        fmt::println("UnitDynamicFlags::IsTappedByAllThreatList");
    }
}

[[maybe_unused]] void dumpDynLowFlagsforGO(const GameObject &entry)
{
    
    uint32_t raw_flags = ReadGameObjectDynamicLowFlags(entry.address);

    
    auto dynlowflags = static_cast<GameObjectDynamicLowFlags>(raw_flags);

    
    fmt::println("Dumping DynLowFlags for object at 0x{:X}:", entry.address);
    fmt::println(" -> Raw Value: 0x{:X}", raw_flags);

    
    

    if ((static_cast<uint32_t>(dynlowflags) & static_cast<uint32_t>(
             GameObjectDynamicLowFlags::GO_DYNFLAG_LO_HIDE_MODEL)) != 0)
    {
        fmt::println(" -> Contains: GO_DYNFLAG_LO_HIDE_MODEL");
    }

    if ((static_cast<uint32_t>(dynlowflags) & static_cast<uint32_t>(GameObjectDynamicLowFlags::GO_DYNFLAG_LO_ACTIVATE))
        != 0)
    {
        fmt::println(" -> Contains: GO_DYNFLAG_LO_ACTIVATE");
    }

    if ((static_cast<uint32_t>(dynlowflags) & static_cast<uint32_t>(GameObjectDynamicLowFlags::GO_DYNFLAG_LO_ANIMATE))
        != 0)
    {
        fmt::println(" -> Contains: GO_DYNFLAG_LO_ANIMATE");
    }

    if ((static_cast<uint32_t>(dynlowflags) & static_cast<uint32_t>(GameObjectDynamicLowFlags::GO_DYNFLAG_LO_DEPLETED))
        != 0)
    {
        fmt::println(" -> Contains: GO_DYNFLAG_LO_DEPLETED");
    }

    if ((static_cast<uint32_t>(dynlowflags) & static_cast<uint32_t>(GameObjectDynamicLowFlags::GO_DYNFLAG_LO_SPARKLE))
        != 0)
    {
        fmt::println(" -> Contains: GO_DYNFLAG_LO_SPARKLE");
    }

    if ((static_cast<uint32_t>(dynlowflags) & static_cast<uint32_t>(GameObjectDynamicLowFlags::GO_DYNFLAG_LO_STOPPED))
        != 0)
    {
        fmt::println(" -> Contains: GO_DYNFLAG_LO_STOPPED");
    }

    if ((static_cast<uint32_t>(dynlowflags) & static_cast<uint32_t>(
             GameObjectDynamicLowFlags::GO_DYNFLAG_LO_NO_INTERACT)) != 0)
    {
        fmt::println(" -> Contains: GO_DYNFLAG_LO_NO_INTERACT");
    }

    if ((static_cast<uint32_t>(dynlowflags) & static_cast<uint32_t>(
             GameObjectDynamicLowFlags::GO_DYNFLAG_LO_INVERTED_MOVEMENT)) != 0)
    {
        fmt::println(" -> Contains: GO_DYNFLAG_LO_INVERTED_MOVEMENT");
    }

    if ((static_cast<uint32_t>(dynlowflags) & static_cast<uint32_t>(GameObjectDynamicLowFlags::GO_DYNFLAG_LO_HIGHLIGHT))
        != 0)
    {
        fmt::println(" -> Contains: GO_DYNFLAG_LO_HIGHLIGHT");
    }

    
    if (raw_flags == 0)
    {
        fmt::println(" -> Contains: NONE");
    }
}

void GetWowTimestampCount()
{
    wow_timestamp = read_memory<int>(TIMESTAMP);
}



std::vector<int> cooldownCache;

void cacheCooldownInformation()
{
    cooldownCache.clear();

    auto CurrentObject = read_memory<uint>(0x00D3F5AC + 0x8);
    while (CurrentObject != 0 && (CurrentObject & 1) == 0)
    {
        auto spellId = read_memory<int>(CurrentObject + 0x8);
        if (!spellId)
        {
            CurrentObject = read_memory<uint>(CurrentObject + 0x4);
            continue;
        }

        auto startTime = read_memory<int>(CurrentObject + 0x10);
        auto cooldown1 = read_memory<int>(CurrentObject + 0x14);
        auto cooldown2 = read_memory<int>(CurrentObject + 0x20);
        auto cooldownLength = std::max(cooldown1, cooldown2);
        if (wow_timestamp - (startTime + cooldownLength) < 0)
        {
            
            
            
            cooldownCache.push_back(spellId);
            
        }

        CurrentObject = read_memory<uint>(CurrentObject + 0x4);
    }
}

bool IsSpellReady(const int target_spell_id)
{
    for (const auto &spell_id: cooldownCache)
    {
        if (spell_id == target_spell_id)
        {
            return true;
        }
    }

    
    return false;
}


uint current_map_id = -1;

bool is_player_in_world()
{
    if (wow_pid == 0) return false;
    
    auto loading_screen = read_memory<int>(0xb2fea8);
    return read_memory<int>(0xBEBA40) == 1 && loading_screen == 0;
}

std::vector<ulong> get_party()
{
    std::vector<ulong> outvec{};

    auto read1 = read_memory<ulong>(0x00BD1948);
    if (read1 != 0)
    {
        outvec.push_back(read1);
    }
    auto read2 = read_memory<ulong>(0x00BD1950);
    if (read2 != 0)
    {
        outvec.push_back(read2);
    }
    auto read3 = read_memory<ulong>(0x00BD1958);
    if (read3 != 0)
    {
        outvec.push_back(read3);
    }
    auto read4 = read_memory<ulong>(0x00BD1960);
    if (read4 != 0)
    {
        outvec.push_back(read4);
    }
    auto read5 = read_memory<ulong>(0x00BD1968);
    if (read5 != 0)
    {
        outvec.push_back(read5);
    }

    return outvec;
}

int ReadRaidCount()
{
    return read_memory<int>(RaidCount);
}


bool solo()
{
    if (ReadRaidCount() == 0 && get_party().size() == 0) return true;

    return false;
}

std::vector<ulong> get_raid(const int raid_count)
{
    std::vector<ulong> outvec(raid_count);
    unsigned int offset = 0;
    for (int i = 0; i < raid_count; ++i)
    {
        auto read2 = read_memory<unsigned int>(RaidArray + offset);
        auto read3 = read_memory<ulong>(read2);
        if (read3 > 0 && read3 < 2000000000000000000)
        {
            outvec.push_back(read3);
        }
        offset += 0x4;
    }

    return outvec;
}

int players = 0;

void UpdateTracks()
{
    
    
    for (const auto &entry: gameobjects_seen)
    {
        if (tracking_hack_enabled)
        {
            if (entry.type == normal_GameObject)
            {
                int flags = read_memory<int>(entry.descriptor + 0x24);
                if (flags == 0 || flags == 32)
                {
                    uint32_t bytes1_address = entry.descriptor + 0x3C;
                    
                    uint32_t current_data = read_memory<uint32_t>(bytes1_address);
                    uint8_t current_type = (current_data >> 8) & 0xFF;
                    if (current_type != 3)
                    {
                        
                        
                        uint32_t new_data = current_data & 0xFFFF00FF;

                        
                        new_data |= (3 << 8);

                        
                        write_memory<uint32_t>(bytes1_address, new_data);
                    }
                }
            }
        }
        
        if (entry.type == Player && entry.guid != resolved_player_guid)
        {
            if (is_player_in_world() && players < 40)
            {
                

                
                
                
                
                
                

                if (auto read1 = read_memory<int>(entry.descriptor + 0x13C); read1 == 0)
                {
                    write_memory(entry.descriptor + 0x13C, TrackUnit); 
                }
            }
            else return;
        }
    }
}

WowClass read_player_class(const GameObject &targetGO)
{
    auto read = read_memory<uint32_t>(targetGO.descriptor + 0x5C);
    return static_cast<WowClass>(static_cast<char>(read >> 8 & 0xFF));
}

float get_rotation_from_substructure(uint32_t GOAddr)
{
    if (!GOAddr) return 0.0f;
    uint32_t subStructPtr = read_memory<uint32_t>(GOAddr + 0xd8);
    if (!subStructPtr) return 0.0f;
    return read_memory<float>(subStructPtr + 0x20);
}


float get_player_rotation()
{
    return get_rotation_from_substructure(ourself.address);
}


float getUnitRotation(const uint &curr)
{
    return get_rotation_from_substructure(curr) + 1.57079632f;
}

std::array<float, 16> read_matrix_16(const uint32_t base_address)
{
    std::array<float, 16> result{};
    constexpr size_t stride = sizeof(float);

    for (size_t i = 0; i < 16; ++i)
    {
        result[i] = read_memory<float>(base_address + i * stride);
    }

    return result;
}

Point getUnitPosition(const uint curr)
{
    const auto OX = read_memory<float>(curr + POS_X);
    const auto OY = read_memory<float>(curr + POS_Y);
    const auto OZ = read_memory<float>(curr + POS_Z);

    
    if (const auto flat_point = Point{OX, OY, OZ}; ourself.position.Distance(flat_point) <= 600) return flat_point;

    
    auto matrixPtr = read_matrix_16(curr + 0x1a8);

    
    

    

    float realX = OX * matrixPtr[0] + OY * matrixPtr[4] + OZ * matrixPtr[8] + matrixPtr[12];
    float realY = OX * matrixPtr[1] + OY * matrixPtr[5] + OZ * matrixPtr[9] + matrixPtr[13];
    float realZ = OX * matrixPtr[2] + OY * matrixPtr[6] + OZ * matrixPtr[10] + matrixPtr[14];

    
    if (realX != 0 && realY != 0 && realZ != 0) return Point{realX, realY, realZ};

    return Point{OX, OY, OZ};
}


uint32_t getRelationshipStatus(const uint32_t selfAddress, const uint32_t targetAddress)
{
    if (!selfAddress || !targetAddress) return 3; 
    if (selfAddress == targetAddress) return 4; 

    const uint32_t self_pUnitData = read_memory<uint32_t>(selfAddress + OFF_pUnitData);
    const uint32_t target_pUnitData = read_memory<uint32_t>(targetAddress + OFF_pUnitData);

    if (!self_pUnitData || !target_pUnitData) return 3; 

    const uint32_t self_combat_flag = read_memory<uint32_t>(self_pUnitData + OFF_unitData_Bitfield_Combatant);
    const uint32_t target_combat_flag = read_memory<uint32_t>(target_pUnitData + OFF_unitData_Bitfield_Combatant);

    const uint32_t self_owner = read_memory<uint32_t>(selfAddress + OFF_pOwnerObject);
    if (self_owner != 0 && self_owner == read_memory<uint32_t>(targetAddress + OFF_pOwnerObject))
    {
        return 4; 
    }

    if (self_combat_flag >> 3 & 1 && target_combat_flag >> 3 & 1)
    {
        
        const uint8_t self_pvp = read_memory<uint8_t>(self_pUnitData + OFF_unitData_Bitfield_Pvp);
        if ((self_pvp & 4) != 0 && (read_memory<uint8_t>(target_pUnitData + OFF_unitData_Bitfield_Pvp) & 4) != 0)
        {
            return 1; 
        }
    }

    const uint32_t factionA_id = read_memory<uint32_t>(self_pUnitData + OFF_unitData_FactionID);
    const uint32_t factionB_id = read_memory<uint32_t>(target_pUnitData + OFF_unitData_FactionID);

    
    const uint32_t factionBaseId = read_memory<uint32_t>(RVA_FactionIdBase);
    const uint32_t factionMaxId = read_memory<uint32_t>(RVA_FactionIdMax);
    if (factionA_id < factionBaseId || factionA_id > factionMaxId || factionB_id < factionBaseId || factionB_id >
        factionMaxId)
    {
        return 3; 
    }

    const uint32_t factionTablePtr = read_memory<uint32_t>(RVA_FactionDataTable);
    if (!factionTablePtr) return 3;
    const uint32_t factionA_ptr = read_memory<uint32_t>(
        factionTablePtr + (factionA_id - factionBaseId) * sizeof(uint32_t));
    const uint32_t factionB_ptr = read_memory<uint32_t>(
        factionTablePtr + (factionB_id - factionBaseId) * sizeof(uint32_t));
    if (!factionA_ptr || !factionB_ptr) return 3;

    
    if ((read_memory<uint32_t>(factionA_ptr + OFF_faction_HostileGroupFlags) & read_memory<uint32_t>(
             factionB_ptr + OFF_faction_GroupMask)) != 0) return 1;

    
    const int32_t parentIdB = read_memory<int32_t>(factionB_ptr + OFF_faction_ParentId);
    for (int i = 0; i < 4; ++i)
    {
        int32_t enemyId = read_memory<int32_t>(factionA_ptr + OFF_faction_ExplicitEnemies + (i * 4));
        if (enemyId == 0) break;
        if (enemyId == parentIdB) return 1;
    }

    
    const int32_t parentIdA = read_memory<int32_t>(factionA_ptr + OFF_faction_ParentId);
    if ((read_memory<uint32_t>(factionA_ptr + OFF_faction_AllyGroupFlags) & read_memory<uint32_t>(
             factionB_ptr + OFF_faction_GroupMask)) != 0) return 4;
    for (int i = 0; i < 4; ++i)
    {
        int32_t allyId = read_memory<int32_t>(factionA_ptr + OFF_faction_ExplicitAllies + (i * 4));
        if (allyId == 0) break;
        if (allyId == parentIdB) return 4;
    }
    if ((read_memory<uint32_t>(factionB_ptr + OFF_faction_AllyGroupFlags) & read_memory<uint32_t>(
             factionA_ptr + OFF_faction_GroupMask)) != 0) return 4;
    for (int i = 0; i < 4; ++i)
    {
        int32_t allyId = read_memory<int32_t>(factionB_ptr + OFF_faction_ExplicitAllies + (i * 4));
        if (allyId == 0) break;
        if (allyId == parentIdA) return 4;
    }

    
    bool isDefaultHostile = (read_memory<uint32_t>(factionA_ptr + OFF_faction_FlagsPvp) >> 12) & 1;
    return isDefaultHostile ? 1 : 3;
}

bool isUnitHostile(const GameObject &target)
{
    
    auto flags = ReadFlagsForGO(target);
    
    if (flags & UNIT_FLAG_IN_COMBAT) return true;

    
    return getRelationshipStatus(ourself.address, target.address) < 2;
}

bool isUnitFriendly(const uint32_t targetAddress)
{
    return getRelationshipStatus(ourself.address, targetAddress) == 4;
}

Relationship getUnitRelationship(const uint32_t testAddress, const uint32_t targetAddress)
{
    const auto relations = getRelationshipStatus(testAddress, targetAddress);
    if (relations < 2)
    {
        return Relationship::Hostile;
    }
    if (relations == 4)
    {
        return Relationship::Friendly;
    }
    return Relationship::Neutral;
}

int CountHostileNPCNearGO(const GameObject &target)
{
    int count = 0;
    for (const auto &entry: gameobjects_seen)
    {
        if (entry.type != Unit) continue;
        if (entry.health < 100) continue; 
        if (target.position.Distance(entry.position) > 10) continue;

        auto flags = ReadDynFlagsForGO(entry);

        if (flags & TaggedByMe || flags & IsTappedByAllThreatList) if (isUnitHostile(entry)) count += 1;
    }
    return count;
}

constexpr const ulong zero_guid = 0;

void GOInteract(const ulong object_id)
{
    write_memory(0x00BD07A0, object_id);
    send_key("]"); 
    std::this_thread::sleep_for(std::chrono::milliseconds(random(20, 40)));
    write_memory(0x00BD07A0, zero_guid);
}

std::map<int, int> values;

void objectscan(unsigned objdescriptor)
{
    for (int i = 1; i < 1500; ++i)
    {
        auto read = read_memory<unsigned char>(objdescriptor + i);
        if (!values.contains(i))
        {
            values.emplace(i, read);
        }
        else
        {
            if (values.at(i) != read) fmt::println("At {} was {} before now {}", i, values.at(i), read);
        }
    }
}

bool automatic_interact = false;

struct InteractionData
{
    time_t firstTime = 0;
    time_t lastInteractionTime = 0;
    int delay = 0;
};

std::unordered_map<unsigned long, InteractionData> interactionMap;
Point last_player_position;
Point last_player_positionInteract;

void checkAndInteract(const unsigned long guid)
{
    if (is_player_looting()) return;

    if (ourself.position.Distance(last_player_positionInteract) > 0.0)
    {
        automatic_interact = false;
        fmt::println("Disabling because of movement");
        return;
    }

    time_t currentTime = std::time(nullptr);
    auto &data = interactionMap[guid];

    if (data.firstTime == 0)
    {
        data.firstTime = currentTime;
        data.delay = random(1, 8);
        data.lastInteractionTime = currentTime;
        return;
    }

    if (currentTime - data.firstTime < data.delay)
    {
        return;
    }

    if (currentTime - data.lastInteractionTime > 20)
    {
        data.lastInteractionTime = currentTime;
        GOInteract(guid);
    }
}

void RescanGameObjectCache()
{
    gameobjects_seen.clear();
    if (!is_player_in_world())
    {
        return;
    }

    auto clientConnection = read_memory<uint>(CLIENT_CONNECTION);
    if (clientConnection == 0 || (clientConnection & 1) != 0)
    {
        return;
    }
    auto objectManager = read_memory<uint>(clientConnection + OBJ_MANAGER);
    if (objectManager == 0 || (objectManager & 1) != 0)
    {
        return;
    }

    auto map_id = read_memory<int>(objectManager + 204);
    if (map_id >= -1)
    {
        current_map_id = map_id;
    }

    objectManager += LIST_START;

    if ((objectManager & 1) != 0)
    {
        return;
    }

    auto curr = read_memory<uint>(objectManager);
    players = 0;

    int total_iters = 0;

    while (total_iters <= 1500)
    {
        if (!curr || (curr & 1) != 0)
        {
            break;
        }
        total_iters += 1;
        auto type = read_memory<WowObjectType>(curr + TYPE);
        ulong guid = read_memory<ulong>(curr + GUID);
        auto objdescriptor = read_memory<uint>(curr + 0x8);

        int health = 0;
        int max_hp = 0;
        int per = 0;
        if (type == Player || type == Unit)
        {
            
            if (guid == resolved_player_guid)
            {
                curr = read_memory<uint>(curr + NEXT);
                continue;
            }

            health = read_memory<int>(objdescriptor + 0x18 * 4);
            max_hp = read_memory<int>(objdescriptor + 0x20 * 4);
            per = percent(health, max_hp);
        }

        auto position = getUnitPosition(curr);
        gameobjects_seen.emplace_back(curr, objdescriptor, type, guid, position, health, max_hp, per);

        if (automatic_interact)
        {
            if (type == normal_GameObject)
            {
                int flags = read_memory<int>(objdescriptor + 0x24);
                
                
                
                
                if (flags == 0 || flags == 32)
                {
                    auto distance = position.Distance(ourself.position);
                    if (distance < 6 && distance >= 0.001)
                    {
                        checkAndInteract(guid);
                    }
                }
            }
        }

        if (automatic_flag && current_map_id == 489)
        {
            auto distance = position.Distance(ourself.position);
            if (distance < 20 && distance >= 0.001)
            {
                
                
                uint pName = read_memory<uint>(curr + 0x1A4);
                uint pStr = read_memory<uint>(pName + 0x90);
                std::string objectName = read_memory(pStr, 40);
                if (objectName == "Warsong Flag" || objectName == "Silverwing Flag")
                {
                    GOInteract(guid);
                }
            }
        }

        if (type == Player)
        {
            players += 1; 
        }

        curr = read_memory<uint>(curr + NEXT);
    }

    if (total_iters >= 1500)
    {
        logWarn("Gameobject caching early exit, gameobject array contained too many pointers");
    }
    UpdateTracks();
}

void resetPlayerPtr()
{
    
    ourself = {};
    playerBase = 0;
    spell_database.clear();
    resolved_player_class = {};
    resolved_player_guid = 0;
    current_map_id = -1;
    gameobjects_seen.clear();
    current_macro_address = 0;
}

void FindPlayerPointer()
{
    if (!is_player_in_world())
    {
        return;
    }
    auto player_guid1 = read_memory<ulong>(PlayerGUID1);
    auto player_guid2 = read_memory<ulong>(PlayerGUID2);
    ulong usable_player_guid = 0;

    if (player_guid1 > 0)
    {
        usable_player_guid = player_guid1;
    }
    if (usable_player_guid == 0 && player_guid2 > 0)
    {
        usable_player_guid = player_guid2;
    }

    
    auto playerptr_test1 = read_memory<uint>(0xcd87a8);
    if (playerptr_test1 == 0) return;
    auto playerptr_test2 = read_memory<uint>(playerptr_test1 + 0x34);
    if (playerptr_test2 == 0) return;
    auto playerptr_test = read_memory<uint>(playerptr_test2 + 0x24);
    playerBase = playerptr_test;

    if (playerptr_test > 0 && (playerptr_test & 1) == 0)
    {
        auto playerPtr = playerptr_test;
        auto descriptor = read_memory<uint>(playerPtr + 0x8);
        resolved_player_guid = usable_player_guid;

        auto health = read_memory<int>(descriptor + 0x18 * 4);
        auto max = read_memory<int>(descriptor + 0x20 * 4);
        auto pct = percent(health, max);

        ourself = GameObject{
            playerPtr, descriptor, Player, usable_player_guid,
            Point{read_memory<float>(PLAYER_POS_X), read_memory<float>(PLAYER_POS_Y), read_memory<float>(PLAYER_POS_Z)},
            health, max, pct
        };

        return;
    }

    logInfo("Player pointer was not found!");
    resetPlayerPtr();
}

int current_channel = 0;

int player_currently_channeling()
{
    return current_channel;
}

int current_cast = 0;

int player_current_casting()
{
    return current_cast;
}

constexpr auto login_state_string = HashedStringFn("login");

bool is_player_logged_in()
{
    std::string readstr{};
    readstr = read_memory(0xB6A9E0, 6);
    if (readstr.empty())
    {
        return false;
    }

    auto comparison = HashedStringFn(readstr);

    return comparison != login_state_string;
}

std::vector<spell> spellbook_database;

bool player_has_spell(const HashedString &input)
{
    for (const auto &entry: spellbook_database)
    {
        if (entry.name == input)
        {
            return true;
        }
    }

    return false;
}

bool setup_spellbook_database()
{
    int spell_count = read_memory<int>(0x00BE8D9C) + 1; 
    spellbook_database.clear();
    if (spell_count <= 1)
    {
        return false;
    }
    spellbook_database.reserve(spell_count);

    unsigned int spellbook_array_addr = 0x00BE5D88;
    for (int i = 0; i != spell_count; ++i)
    {
        auto read3 = read_memory<int>(spellbook_array_addr);
        spellbook_array_addr += 0x4;
        if (read3 <= 0)
        {
            continue;
        }

        auto it = spell_database.find(read3);
        if (it == spell_database.end())
        {
            continue;
        }

        const auto &spell_info = it->second;

        spellbook_database.emplace_back(spell_info);

#ifdef DEBUG
        logWarn("Player spell {} {}", spell_info, read3);
#endif
    }

    
    if (resolved_player_class == Hunter || resolved_player_class == Warlock)
    {
        unsigned int pet_spellbook_array_addr = 0xBE7D88;
        for (int i = 0; i <= 30; ++i)
        {
            auto read3 = read_memory<int>(pet_spellbook_array_addr);
            pet_spellbook_array_addr += 0x4;
            if (read3 == 45927)
            {
                continue;
            } 
            if (read3 == 7355)
            {
                continue;
            } 
            if (read3 == 7266)
            {
                continue;
            } 
            if (read3 == 0)
            {
                continue;
            }

            auto it = spell_database.find(read3);
            if (it == spell_database.end())
            {
                continue;
            }

            const auto &spell_info = it->second;

            spellbook_database.push_back(spell_info);

            logWarn("Pet spell {} {}", spell_info, read3);
        }
    }

    return true;
}

uint32_t original_castbar_spell1;
uint32_t original_castbar_spell2;
uint32_t original_castbar_spell3;
uint32_t original_castbar_spell4;
uint32_t original_castbar_spell5;
uint32_t original_castbar_spell6;
uint32_t original_castbar_spell7;

void read_original_actionbar_spells()
{
    original_castbar_spell1 = read_memory<uint32_t>(FirstActionBarSpellId);
    original_castbar_spell2 = read_memory<uint32_t>(FirstActionBarSpellIdDepths);
    original_castbar_spell3 = read_memory<uint32_t>(FirstActionBarBearForm_and_WarriorBeserkerStance);
    original_castbar_spell4 = read_memory<uint32_t>(FirstActionBarCatForm_ShadowForm_and_WarriorBattleStance);
    original_castbar_spell5 = read_memory<uint32_t>(FirstActionBarRestoTreeForm_and_WarriorDefensiveStance);
    original_castbar_spell6 = read_memory<uint32_t>(FirstActionBarDruidStealth);
    original_castbar_spell7 = read_memory<uint32_t>(FirstActionBarRogueStealth);
}

void reset_actionbar()
{
    write_memory(FirstActionBarSpellId, original_castbar_spell1);
    write_memory(FirstActionBarSpellIdDepths, original_castbar_spell2);
    write_memory(FirstActionBarBearForm_and_WarriorBeserkerStance, original_castbar_spell3);
    write_memory(FirstActionBarCatForm_ShadowForm_and_WarriorBattleStance, original_castbar_spell4);
    write_memory(FirstActionBarRestoTreeForm_and_WarriorDefensiveStance, original_castbar_spell5);
    write_memory(FirstActionBarDruidStealth, original_castbar_spell6);
    write_memory(FirstActionBarRogueStealth, original_castbar_spell7);
}

bool IsOnCooldown(const HashedString &spell_name)
{
    int found_spell = 0;
    for (const auto &entry: spellbook_database)
    {
        if (entry.name == spell_name)
        {
            found_spell = entry.id;
            break;
        }
    }

    if (found_spell)
    {
        return IsSpellReady(found_spell);
    }
    else
    {
#if DEBUG
        logWarn("Did not find spell in cooldownchecker from playerspells {}", spell_name);
#endif
    }

    return false;
}

bool CheckSpell(const HashedString &spell_name)
{
    return player_has_spell(spell_name) && !IsOnCooldown(spell_name);
}

void simplecast(const HashedString &spell_name, const bool repeat = true)
{
}

bool cast_spell_at_feet(const HashedString &spell_name)
{
    if (last_spell_out == spell_name.get_hash()) return true;
    SetFirstSlotToMacro("test1");
    auto address = GetMacroBodyAddress("test1");
    if (current_macro_address != address)
    {
        current_macro_address = address;
        fmt::println("Macro body addr change {:x}", address);
    }
    if (address)
    {
        if (ReplaceMacroBody(address, MacroGen::CastSelf(spell_name.get_view()))) send_key("1");
    }
    return false;
}


bool hardcast(const HashedString &spell_name, const bool repeat = true)
{
    if (last_spell_out == spell_name.get_hash()) return true;
    SetFirstSlotToMacro("test1");
    auto address = GetMacroBodyAddress("test1");
    if (current_macro_address != address)
    {
        current_macro_address = address;
        fmt::println("Macro body addr change {:x}", address);
    }
    if (address)
    {
        if (ReplaceMacroBody(address, MacroGen::CastMeleeStart(spell_name.get_view()))) send_key("1");
    }
    return false;
#ifdef DEBUG
    bool found_in_db = false;
    for (const auto &entry: spell_database)
    {
        if (entry.second.name == spell_name)
        {
            found_in_db = true;
            break;
        }

        if (entry.second.name.get_view() == spell_name.get_view())
        {
            logWarn("Found error in spell comparison {} {} ", spell_name.get_hash(), entry.second.name.get_hash());
        }
    }
    if (!found_in_db)
    {
        logWarn("hardcast attempted on spell not present in dbc {}", spell_name);
    }
#endif

#if DEBUG
    
#endif
    int found_spell = 0;

    for (const auto &entry: spellbook_database)
    {
        if (entry.name == spell_name)
        {
            
            found_spell = entry.id;
            break;
        }
    }

    if (found_spell)
    {
        if (!repeat)
        {
            if (last_spell_out == found_spell)
            {
#if DEBUG
                logWarn("Repeated cast prevented {} {}", found_spell, spell_name);
#endif
                return true;
            }
        }

#if DEBUG
        
#endif
        write_memory(selected_castbar_addr, found_spell);
        write_memory(FirstActionBarSpellIdDepths, found_spell);
        send_key("1");
        return false;
    }

    
#if DEBUG
    logWarn("Hardcast Spell not found {} {}", spell_name, spell_name.get_hash());
#endif
    return false;
}

void SetTargetGuid(ulong guid)
{
    
    
    
    write_memory(0x00BD07B0, guid);
}


void SetMouseoverGuid(const ulong &guid)
{
    write_memory(0x00BD07A0, guid);
}

ulong GetMouseoverGuid()
{
    return read_memory<ulong>(0x00BD07A0);
}

ulong TargetGuid()
{
    return read_memory<ulong>(0x00BD07B0);
}

void cast_mouseover(const HashedString &spell_name, const ulong guid)
{
    SetFirstSlotToMacro("test1");
    SetMouseoverGuid(guid);
    auto address = GetMacroBodyAddress("test1");
    if (current_macro_address != address)
    {
        current_macro_address = address;
        fmt::println("Macro body addr change {:x}", address);
    }
    if (address)
    {
        if (ReplaceMacroBody(address, MacroGen::CastMouseover(spell_name.get_view()))) send_key("1");
    }
}

void modify_castbar_and_cast_self(const HashedString &spell_name)
{
    SetFirstSlotToMacro("test1");
    SetMouseoverGuid(ourself.guid);
    auto address = GetMacroBodyAddress("test1");
    if (current_macro_address != address)
    {
        current_macro_address = address;
        fmt::println("Macro body addr change {:x}", address);
    }
    if (address)
    {
        if (ReplaceMacroBody(address, MacroGen::CastSelf(spell_name.get_view()))) send_key("1");
    }
    return;

    auto target_id = TargetGuid();
    if (target_id != resolved_player_guid)
    {
        SetTargetGuid(resolved_player_guid);
    }

    hardcast(spell_name);
}

void stop_casting()
{
    SetFirstSlotToMacro("test1");
    SetMouseoverGuid(0);
    auto address = GetMacroBodyAddress("test1");
    if (address)
    {
        if (ReplaceMacroBody(address, MacroGen::StopCasting())) send_key("1");
    }
}


bool castHeal(const HashedString &spell_name, const ulong targetguid, const bool repeat = true)
{
    SetFirstSlotToMacro("test1");
    SetMouseoverGuid(targetguid);
    auto address = GetMacroBodyAddress("test1");
    if (current_macro_address != address)
    {
        current_macro_address = address;
        fmt::println("Macro body addr change {:x}", address);
    }
    if (address)
    {
        if (ReplaceMacroBody(address, MacroGen::CastMouseover(spell_name.get_view())))
        {
            last_healer_target_guid = targetguid;
            send_key("1");
            return true;
        }
    }

    return true;
    bool retstate = true;
    auto target_id = TargetGuid();
    int found_spell = 0;
    for (const auto &entry: spellbook_database)
    {
        if (entry.name == spell_name)
        {
            
            found_spell = entry.id;
            break;
        }
    }

    if (found_spell)
    {
        if (!repeat)
        {
            
            if (last_healer_target_guid == target_id && last_spell_out == found_spell)
            {
                return false; 
            }
        }

        auto cd_info = IsOnCooldown(spell_name);

        
        if (!cd_info)
        {
            retstate = true;
        }

        if (target_id != targetguid)
        {
            SetTargetGuid(targetguid);
        }
        last_healer_target_guid = target_id;

        
        write_memory(selected_castbar_addr, found_spell);
        write_memory(FirstActionBarSpellIdDepths, found_spell);
        send_key("1");
        return retstate;
    }

    
#if DEBUG
    fmt::println("castHeal Spell not found {}", spell_name);
#endif
    return false;
}

void request_aura_cache_for(GameObject &entry)
{
    if (entry.type != Player && entry.type != Unit) return;
    if (!entry.auras.empty()) return;

    
    int auraCount = read_memory<int>(entry.address + AURA_COUNT_1);
    uint auraTable = entry.address + AURA_TABLE_1;

    
    if (auraCount == -1)
    {
        auraCount = read_memory<int>(entry.address + AURA_COUNT_2);
        auraTable = read_memory<uint>(entry.address + AURA_TABLE_2); 
    }

    if (auraCount > 255) auraCount = 255;
    if (auraCount < 1) return;

    for (unsigned int i = 0; i < auraCount; ++i)
    {
        
        auto aura_base_addr = auraTable + (AURA_SIZE * i);

        int SpellID = read_memory<int>(aura_base_addr + AURA_SPELL_ID);
        if (SpellID <= 0) continue;

        
        auto it = spell_database.find(SpellID);
        if (it == spell_database.end()) continue;

        const auto &spell_info = it->second;
        aura_complete retval;
        retval.opspell_hash = spell_info.name.get_hash();
        retval.spell_id = SpellID;

        
        retval.spell_caster = read_memory<ulong>(aura_base_addr + AURA_SPELL_OWNER);

        
        int spell_endtime = read_memory<int>(aura_base_addr + AURA_SPELL_ENDTIME);

        
        
        auto raw_stackcount = read_memory<unsigned char>(aura_base_addr + AURA_SPELL_STACKCOUNT);
        retval.stack_count = static_cast<int>(raw_stackcount);

        
        
        int remaining = spell_endtime - wow_timestamp;

        
        
        
        if (remaining < 0)
        {
            remaining = 0;
            
        }

        auto divide = remaining / 1000;
        if (divide <= 0) divide = 1; 

        retval.time_left = divide;
        entry.auras.push_back(retval);
    }
}

struct aura_information_max
{
    bool good = false;
    int stack_count = 0;
    int time_left = 0;
};

aura_information_max findAuraMax(GameObject &object, const HashedString &spell, bool only_my_own_aura = false)
{
    request_aura_cache_for(object);
    aura_information_max retval{};
    for (const auto &entry: object.auras)
    {
        if (entry.opspell_hash == spell.get_hash())
        {
            if (only_my_own_aura)
            {
                if (entry.spell_caster == resolved_player_guid)
                {
                    retval.good = true;
                    retval.stack_count = entry.stack_count;
                    retval.time_left = entry.time_left;
                    return retval;
                }
                else
                {
                    continue;
                }
            }
            else
            {
                retval.good = true;
                retval.stack_count = entry.stack_count;
                retval.time_left = entry.time_left;
                return retval;
            }
        }
    }
    return retval;
}

bool findAura(GameObject &object, const HashedString &spell, bool only_my_own_aura = false)
{
#ifdef DEBUG
    bool found_in_db = false;
    for (const auto &entry: spell_database)
    {
        if (found_in_db)
        {
            break;
        }
        if (entry.second.name == spell)
        {
            found_in_db = true;
            break;
        }
    }
    if (!found_in_db)
    {
        logWarn("Findaura attempted on spell not present in dbc {} {}", spell, spell.get_hash());
    }
#endif
    request_aura_cache_for(object);
    for (const auto &entry: object.auras)
    {
        if (entry.opspell_hash != spell.get_hash())
        {
            continue;
        }
        if (only_my_own_aura)
        {
            if (entry.spell_caster == resolved_player_guid)
            {
                return true;
            }
            else
            {
                continue;
            }
        }
        else
        {
            return true;
        }
    }

    return false;
}

int findAuraTimeLeft(GameObject &object, const HashedString &spell, bool only_my_own_aura = false)
{
#ifdef DEBUG
    bool found_in_db = false;
    for (const auto &entry: spell_database)
    {
        if (found_in_db)
        {
            break;
        }
        if (entry.second.name == spell)
        {
            found_in_db = true;
            break;
        }
    }
    if (!found_in_db)
    {
        logWarn("Findaura attempted on spell not present in dbc {}", spell);
    }
#endif
    request_aura_cache_for(object);
    for (const auto &entry: object.auras)
    {
        if (entry.opspell_hash != spell.get_hash())
        {
            continue;
        }

        if (only_my_own_aura)
        {
            if (entry.spell_caster == resolved_player_guid)
            {
                return entry.time_left;
            }
            else
            {
                continue;
            }
        }
        else
        {
            return entry.time_left;
        }
    }

    return 0;
}

bool findAuraOnOurself(const int spell, bool only_my_own_aura = false)
{
#ifdef DEBUG
    bool found_in_db = false;
    for (const auto &entry: spell_database)
    {
        if (found_in_db)
        {
            break;
        }
        if (entry.second.id == spell)
        {
            found_in_db = true;
            break;
        }
    }
    if (!found_in_db)
    {
        logWarn("findAuraOnOurself attempted on spell not present in dbc {}", spell);
    }
#endif
    for (const auto &entry: ourself.auras)
    {
        if (entry.spell_id != spell)
        {
            continue;
        }
        if (only_my_own_aura)
        {
            if (entry.spell_caster == resolved_player_guid)
            {
                return true;
            }
            else
            {
                continue;
            }
        }
        else
        {
            return true;
        }
    }

    return false;
}

bool findAuraOnOurself(const HashedString &spell, bool only_my_own_aura = false)
{
#ifdef DEBUG
    bool found_in_db = false;
    for (const auto &entry: spell_database)
    {
        if (found_in_db)
        {
            break;
        }
        if (entry.second.name == spell)
        {
            found_in_db = true;
            break;
        }
    }
    if (!found_in_db)
    {
        logWarn("Findaura attempted on spell not present in dbc {}", spell);
    }
#endif
    for (const auto &entry: ourself.auras)
    {
        if (entry.opspell_hash != spell.get_hash())
        {
            continue;
        }
        if (only_my_own_aura)
        {
            if (entry.spell_caster == resolved_player_guid)
            {
                return true;
            }
            else
            {
                continue;
            }
        }
        else
        {
            return true;
        }
    }

    return false;
}


bool FindAuraByParty(std::vector<GameObject> &party, const HashedString &input, bool only_my_own_aura = false)
{
    for (auto &entry: party)
    {
        if (findAura(entry, input, only_my_own_aura))
        {
            return true;
        }
    }
    return false;
}

std::chrono::time_point time_since_last_new_bobber = std::chrono::steady_clock::now();

bool isPlayerFishing()
{
    return findAuraOnOurself(fishing_spell);
}

ulong current_bobber = 0;

void interact_bobber(const ulong &bobberguid)
{
    GOInteract(bobberguid);
}

std::chrono::time_point last_fishing_attempt = std::chrono::steady_clock::now();

HashedString fishing_bobber("Fishing Bobber");

void fishing_poll()
{
    if (is_player_looting())
    {
        current_bobber = 0;
        logWarn("Player looting");
        return;
    }

    if (!isPlayerFishing())
    {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fishing_attempt).count() <=
            random(2000, 3000)) return;
        hardcast(fishing_spell);
        std::this_thread::sleep_for(std::chrono::milliseconds(random(1000, 2000)));
        last_fishing_attempt = now;
        return;
    }
    else
    {
        for (const auto &entry: gameobjects_seen)
        {
            if (entry.type != normal_GameObject)
            {
                continue;
            }
            auto summoner = read_memory<ulong>(entry.address + 560);

            if (summoner != resolved_player_guid)
            {
                continue; 
            }

            Point other = Point{
                read_memory<float>(entry.address + 0xE8), read_memory<float>(entry.address + 0xEC),
                read_memory<float>(entry.address + 0xF0)
            };
            auto d = ourself.position.Distance(other);
            if (d < 40 && d > 0.1)
            {
                
                uint pName = read_memory<uint>(entry.address + 0x1A4);
                uint pStr = read_memory<uint>(pName + 0x90);
                std::string objectName = read_memory(pStr, 20);
                int flags = read_memory<int>(entry.descriptor + 0x24);
                HashedString objectNameCheck = HashedString::create(objectName);

                if (objectNameCheck == fishing_bobber)
                {
                    current_bobber = entry.guid;
                    if (read_memory<int>(entry.address + BOBBING) != 8650752 && flags == 32) 
                    {
                        
                        std::this_thread::sleep_for(std::chrono::milliseconds(random(1, 400)));
                        
                        if (random(1, 100) >= 96) return;
                        interact_bobber(entry.guid);
                        std::this_thread::sleep_for(std::chrono::milliseconds(random(3000, 4000)));
                        last_fishing_attempt = now;
                        return;
                    }
                    return;
                }
            }
        }
    }
}

ulong TargetOfTarget(const ulong ttarget_guid)
{
    GameObject targetGO{};
    findGameObjectByGUID(ttarget_guid, targetGO);
    return read_memory<ulong>(targetGO.descriptor + 0x48);
}

ulong TargetOfTarget(const GameObject &targetGO)
{
    return read_memory<ulong>(targetGO.descriptor + 0x48);
}

std::vector<const char *> random_key_presses = {"2", "3", "4", "5", "6", "7", "8", "9", "0", "minus", "equal"};

void RandomRotation()
{
    std::shuffle(std::begin(random_key_presses), std::end(random_key_presses), rng);
    send_key_queue(random_key_presses);
}

bool main_tank_present(const std::vector<ulong> &input)
{
    for (const auto &entry: input)
    {
        if (entry == main_tank_guid)
        {
            return true;
        }
    }

    return false;
}

std::vector<GameObject> groupmembers(const int percent_filter = 100)
{
    std::vector<ulong> found;
    std::vector<GameObject> raidstorage;
    auto partystuff = get_party();
    auto raid_raw = get_raid(ReadRaidCount());

    if (main_tank_guid && !main_tank_present(partystuff) && !main_tank_present(raid_raw))
    {
        logWarn("Main tank is no longer present, resetting MT");
        main_tank_guid = 0;
    }

    for (const auto &entry: partystuff)
    {
        if (entry == resolved_player_guid)
        {
            continue;
        }
        if (std::find(found.begin(), found.end(), entry) != found.end())
        {
            continue;
        }

        GameObject targetGO{};
        if (findGameObjectByGUID(entry, targetGO))
        {
            if (targetGO.health_pct >= percent_filter || targetGO.health_pct <= 0)
            {
                continue;
            }
            auto distance = ourself.position.Distance(targetGO.position);
            
            
            if (distance > 41)
            {
                continue;
            }
            found.push_back(entry);

            raidstorage.push_back(targetGO);
        }
    }

    for (const auto &entry: raid_raw)
    {
        if (entry == resolved_player_guid)
        {
            continue;
        }
        if (std::find(found.begin(), found.end(), entry) != found.end())
        {
            continue;
        }
        GameObject targetGO{};
        if (findGameObjectByGUID(entry, targetGO))
        {
            if (targetGO.health_pct >= percent_filter || targetGO.health_pct == 0)
            {
                continue;
            }

            auto distance = ourself.position.Distance(targetGO.position);
            
            
            if (distance > 41)
            {
                continue;
            }
            found.push_back(entry);

            raidstorage.push_back(targetGO);
        }
    }

    
    
    ulong focus_target = read_memory<ulong>(0x00BD07D0);
    if (focus_target != 0)
    {
        if (std::find(found.begin(), found.end(), focus_target) == found.end())
        {
            GameObject focus_GO{};
            if (findGameObjectByGUID(focus_target, focus_GO, notype))
            {
                auto distance = ourself.position.Distance(focus_GO.position);
                
                
                if (distance < 40)
                {
                    raidstorage.push_back(focus_GO);
                }
            }
        }
    }

    
    
    if (ourself.health_pct != 0)
    {
        raidstorage.push_back(ourself);
    }

    
    std::sort(raidstorage.begin(), raidstorage.end(), sortGameobject);

    return raidstorage;
}

bool disengage_because_flags(const GameObject &targetGO)
{
    auto flags = ReadFlagsForGO(targetGO);
    if (flags & UNIT_FLAG_NON_ATTACKABLE)
    {
        return true;
    }

    if (flags & UNIT_FLAG_POSSESSED)
    {
        return true;
    }

    if (flags & UNIT_FLAG_IMMUNE_TO_PC)
    {
        return true;
    }

    if (flags & UNIT_FLAG_NOT_SELECTABLE)
    {
        return true;
    }

    if (flags & UNIT_FLAG_NON_ATTACKABLE_2)
    {
        return true;
    }

    uint8_t low_byte_flags = read_memory<uint8_t>(targetGO.descriptor + 0xF0);
    if (low_byte_flags & (uint8_t) GameObjectDynamicLowFlags::GO_DYNFLAG_LO_HIDE_MODEL)
    {
        fmt::println("Invisible creature target on check DBF -- {} {}", getGameObjectorPlayerName(targetGO),
                     low_byte_flags);
        return true;
    }

    return false;
}

struct thing_needs_taunting
{
    GameObject monster;
    GameObject target_of_target;
};

std::vector<thing_needs_taunting> GetNPCsNotTargetingMe()
{
    std::vector<thing_needs_taunting> retvec;

    for (auto &entry: gameobjects_seen)
    {
        if (entry.type != Unit)
        {
            continue;
        }

        auto distance = ourself.position.Distance(entry.position);

        if (distance > 30)
        {
            continue;
        }

        if (disengage_because_flags(entry))
        {
            continue;
        }

        auto target_of_target = TargetOfTarget(entry);
        if (!target_of_target)
        {
            continue;
        }

        if (target_of_target == ourself.guid) continue;

        GameObject targetGO{};
        if (findGameObjectByGUID(target_of_target, targetGO, Player))
        {
            retvec.emplace_back(thing_needs_taunting{entry, targetGO});
        }
    }

    return retvec;
}

int CountNPCTargetingPlayers()
{
    int returnint = 0;

    for (const auto &entry: gameobjects_seen)
    {
        if (entry.type != Unit)
        {
            continue;
        }

        auto distance = ourself.position.Distance(entry.position);

        if (distance > 7)
        {
            continue;
        }

        if (disengage_because_flags(entry))
        {
            continue;
        }

        auto target_of_target = TargetOfTarget(entry);
        if (!target_of_target)
        {
            continue;
        }

        GameObject targetGO{};
        if (findGameObjectByGUID(target_of_target, targetGO, Player))
        {
            returnint += 1;
        }
    }

    return returnint;
}



int CountNPCTargetingPlayer(const ulong target)
{
    if (target == 0) return 0;
    int retint = 0;
    for (const auto &entry: gameobjects_seen)
    {
        if (entry.type != Unit)
        {
            continue;
        }

        auto distance = ourself.position.Distance(entry.position);

        if (distance > 30)
        {
            continue;
        }

        if (TargetOfTarget(entry) != target)
        {
            continue;
        }

        if (disengage_because_flags(entry))
        {
            continue;
        }

        retint += 1;
    }

    return retint;
}

std::vector<GameObject> findNearbyUnits()
{
    std::vector<GameObject> retvec;
    for (const auto &entry: gameobjects_seen)
    {
        if (entry.type != Unit)
        {
            continue;
        }

        auto distance = ourself.position.Distance(entry.position);

        if (distance > 5)
        {
            continue;
        }

        if (disengage_because_flags(entry))
        {
            continue;
        }

        retvec.push_back(entry);
    }

    return retvec;
}


std::vector<GameObject> CountNPCTargetingMe()
{
    std::vector<GameObject> retvec;

    for (const auto &entry: gameobjects_seen)
    {
        if (entry.type != Unit)
        {
            continue;
        }

        if (TargetOfTarget(entry) != resolved_player_guid)
        {
            continue;
        }

        auto distance = ourself.position.Distance(entry.position);

        if (distance > 8)
        {
            continue;
        }

        if (disengage_because_flags(entry))
        {
            continue;
        }

        retvec.push_back(entry);
    }

    return retvec;
}

std::vector<GameObject> CountPlayers()
{
    std::vector<GameObject> retvec;
    for (const auto &entry: gameobjects_seen)
    {
        GameObject input{};

        if (entry.type != Player)
        {
            continue;
        }

        retvec.push_back(input);
    }

    return retvec;
}


std::chrono::time_point last_self_buff = std::chrono::steady_clock::now();

boost::circular_buffer<ulong> wartank_targets_taunted(10);

void warrior_tank_rotation()
{
    auto current_target = TargetGuid();
    GameObject current_targetGO{};

    auto enemies = CountNPCTargetingMe();

    logWarn("Number of enemies near player {}", enemies.size());
    bool aoe_mode = false;
    if (enemies.size() > 3) 
    {
        aoe_mode = true;
    }

    auto sunder_source = sunder_armor;
    if (player_has_spell(devastate_spell)) sunder_source = devastate_spell;

    auto found_target = findGameObjectByGUID(current_target, current_targetGO, notype);

    if (!found_target || current_targetGO.health_pct <= 0)
    {
        return;
    }

    aura_information_max sunder_info = findAuraMax(current_targetGO, sunder_armor);
    if (player_has_spell(commanding_shout_spell))
    {
        if (!findAuraOnOurself(commanding_shout_spell))
        {
            hardcast(commanding_shout_spell);
            logWarn("Casting commanding shout");
            return;
        }
    }
    else
    {
        if (!findAuraOnOurself(battle_shout_spell))
        {
            logWarn("Casting battle shout");
            hardcast(battle_shout_spell);
            return;
        }
    }

    
    
    
    

    auto flags = ReadFlagsForGO(current_targetGO);
    bool prev_taunted = false;
    for (const auto &entry: wartank_targets_taunted)
    {
        if (entry == current_target)
        {
            prev_taunted = true;
            break;
        }
    }
    auto target_of_target = TargetOfTarget(current_targetGO);
    auto distance = ourself.position.Distance(current_targetGO.position);
    if (distance > 8)
    {
        logWarn("Resetting taunt limit");
        prev_taunted = false;
        wartank_targets_taunted.clear();
    }
    if ((!IsOnCooldown(taunt_spell) || !IsOnCooldown(mocking_blow_spell)) && automatic_taunting)
    {
        if (!prev_taunted && (current_targetGO.type != Player && (
                                  !InCombat() || (target_of_target != 0 && target_of_target != resolved_player_guid))))
        {
            wartank_targets_taunted.clear();
            wartank_targets_taunted.push_back(current_target);
            
            if (!(flags & UNIT_FLAG_FLEEING))
            {
                if (IsOnCooldown(mocking_blow_spell) || distance > 8) hardcast(taunt_spell);
                else hardcast(mocking_blow_spell);
                logWarn("Taunted");
                return;
            }
            
        }
    }

    auto current_rage = ragepower();
    if (current_rage > 50 && aoe_mode)
    {
        hardcast(cleave_spell);
    }

    if (distance < 8)
    {
        if (findAuraTimeLeft(current_targetGO, thunder_clap_spell) < 2 && !IsOnCooldown(thunder_clap_spell))
        {
            hardcast(thunder_clap_spell);
            logWarn("Thunderclapping");
            return;
        }

        if (!IsOnCooldown(shield_block_spell))
        {
            logWarn("Casting shield block");
            hardcast(shield_block_spell);
            return;
        }
    }

    if (!aoe_mode && sunder_info.good)
    {
        if (sunder_info.stack_count < 1 || sunder_info.time_left <= 1)
        {
            hardcast(sunder_source);
        }
    }
    else 
    {
        hardcast(sunder_source);
    }

    
    if (findAuraOnOurself(revenge_aura) && !IsOnCooldown(revenge_spell))
    {
        hardcast(revenge_spell);
        logWarn("Revenge");
        return;
    }

    if (findAuraOnOurself(sword_and_board_spell, true) && !IsOnCooldown(shield_slam_spell))
    {
        hardcast(shield_slam_spell);
        logWarn("Shield slam");
        return;
    }

    if (!findAura(current_targetGO, rend_spell))
    {
        hardcast(rend_spell);
        logWarn("Rending");
        return;
    }

    
    if (!findAuraOnOurself(glyph_of_blocking) && !IsOnCooldown(shield_slam_spell))
    {
        hardcast(shield_slam_spell);
        return;
    }

    if (!aoe_mode && sunder_info.good)
    {
        if (sunder_info.stack_count < 5 || sunder_info.time_left <= 5)
        {
            hardcast(sunder_source);
        }
    }
    else 
    {
        hardcast(sunder_source);
    }

    

    if (distance < 8)
    {
        if (!(flags & UNIT_FLAG_STUNNED))
        {
            if (!IsOnCooldown(concussion_blow_spell))
            {
                hardcast(concussion_blow_spell);
                return;
            }

            if (!IsOnCooldown(shockwave_spell))
            {
                hardcast(shockwave_spell);
                return;
            }
        }

        logWarn("case 2");
    }

    if (!IsOnCooldown(shield_bash_spell))
    {
        hardcast(shield_bash_spell);
        logWarn("Shield bashing");
        return;
    }

    if (aoe_mode)
    {
        if (distance < 8)
        {
            if (current_rage > 50 && !IsOnCooldown(thunder_clap_spell))
            {
                hardcast(thunder_clap_spell);
                logWarn("Spamming thunder clap");
                return;
            }
        }
    }

    if (current_rage > 50)
    {
        hardcast(devastate_spell);
    }
}

void warrior_armstank_rotation()
{
    auto current_target = TargetGuid();
    GameObject current_targetGO{};

    
    auto enermies = CountNPCTargetingMe();
    logWarn("Number of enemies near player {}", enermies.size());
    bool aoe_mode = false;
    if (enermies.size() > 2)
    {
        aoe_mode = true;
    }

    auto found_target = findGameObjectByGUID(current_target, current_targetGO, notype);

    if (player_has_spell(commanding_shout_spell))
    {
        if (!findAuraOnOurself(commanding_shout_spell))
        {
            hardcast(commanding_shout_spell);
        }
    }
    else
    {
        if (!findAuraOnOurself(battle_shout_spell))
        {
            hardcast(battle_shout_spell);
        }
    }

    
    
    
    

    if (!found_target)
    {
        return;
    }

    auto flags = ReadFlagsForGO(current_targetGO);
    bool prev_taunted = false;
    for (const auto &entry: wartank_targets_taunted)
    {
        if (entry == current_target)
        {
            prev_taunted = true;
            break;
        }
    }
    auto target_of_target = TargetOfTarget(current_targetGO);
    auto distance = ourself.position.Distance(current_targetGO.position);
    if (distance > 8)
    {
        prev_taunted = false;
    }
    if (!prev_taunted && (current_targetGO.type != Player && (
                              !InCombat() || (target_of_target != 0 && target_of_target != resolved_player_guid))))
    {
        wartank_targets_taunted.push_back(current_target);
        
        if (!(flags & UNIT_FLAG_FLEEING))
        {
            hardcast(taunt_spell);
        }
        
    }

    auto current_rage = ragepower();
    if (distance < 4)
    {
        if (findAuraTimeLeft(current_targetGO, thunder_clap_spell) < 2)
        {
            hardcast(thunder_clap_spell);
            return;
        }
    }

    if (distance < 5)
    {
        hardcast(shield_block_spell);
    }

    hardcast(mortal_strike_spell);

    
    if (findAuraOnOurself(revenge_aura) && !IsOnCooldown(revenge_spell))
    {
        hardcast(revenge_spell);
        return;
    }

    if (!findAura(current_targetGO, rend_spell))
    {
        hardcast(rend_spell);
        return;
    }

    
    if (!findAuraOnOurself(glyph_of_blocking, true))
    {
        hardcast(shield_slam_spell);

        return;
    }

    if (distance < 4)
    {
        if (!findAura(current_targetGO, demoralizing_shout_spell))
        {
            hardcast(demoralizing_shout_spell);
        }
    }

    if (distance < 4)
    {
        if (current_rage > 50)
        {
            hardcast(thunder_clap_spell);
        }
    }

    if (!aoe_mode)
    {
        if (current_rage > 30)
        {
            hardcast(mocking_blow_spell);
        }
    }
    else
    {
        
        hardcast(thunder_clap_spell);

        if (current_rage > 50)
        {
            hardcast(cleave_spell);
        }
    }

    if (current_rage > 11)
    {
        hardcast(sunder_armor);
    }
}

void PaladinTankRotation()
{
    auto current_target = TargetGuid();
    GameObject current_targetGO{};
    auto found_target = findGameObjectByGUID(current_target, current_targetGO);

    auto nearby_npcs = GetNPCsNotTargetingMe();

    for (const auto &entry: nearby_npcs)
    {
        if (!IsOnCooldown(hand_of_reckoning_spell))
        {
            cast_mouseover(hand_of_reckoning_spell, entry.monster.guid);
            break;
        }

        if (!IsOnCooldown(righteous_defense_spell))
        {
            cast_mouseover(righteous_defense_spell, entry.target_of_target.guid);
            break;
        }
    }

    if (!findAuraOnOurself(seal_of_command_spell))
    {
        hardcast(seal_of_command_spell);
        return;
    }

    if (!findAuraOnOurself(righteous_fury))
    {
        hardcast(righteous_fury);
        return;
    }

    if (InCombat() && !IsOnCooldown(divine_sacrifice_spell))
    {
        const auto group_stuff = groupmembers(70).size();
        if (group_stuff > 3)
        {
            hardcast(divine_sacrifice_spell);
            return;
        }
    }

    if (!findAuraOnOurself(blessing_of_sanctuary_spell) && !findAuraOnOurself(greater_blessing_of_sanctuary_spell))
    {
        if (current_target == resolved_player_guid || current_target == 0)
        {
            hardcast(blessing_of_sanctuary_spell);
        }
        else
        {
            modify_castbar_and_cast_self(blessing_of_sanctuary_spell);
            SetTargetGuid(current_target);
        }
        return;
    }

    if (!findAuraOnOurself(sacred_shield_spell))
    {
        hardcast(sacred_shield_spell);
        return;
    }

    if (!found_target) return;

    auto distance = ourself.position.Distance(current_targetGO.position);

    if (distance >= 20) return;

    if (!IsOnCooldown(judgement_of_light_spell) && !IsOnCooldown(judgement_of_wisdom_spell) && !IsOnCooldown(
            judgement_of_justice))
    {
        if (findAura(current_targetGO, judgement_of_wisdom_spell, true))
        {
            hardcast(judgement_of_wisdom_spell);
            return;
        }
        if (findAura(current_targetGO, judgement_of_light_spell, true))
        {
            hardcast(judgement_of_light_spell);
            return;
        }
        if (!findAura(current_targetGO, judgement_of_wisdom_spell))
        {
            hardcast(judgement_of_wisdom_spell);
            return;
        }
        if (!findAura(current_targetGO, judgement_of_light_spell))
        {
            hardcast(judgement_of_light_spell);
            return;
        }

        hardcast(judgement_of_justice);
        return;
    }

    if (current_targetGO.health_pct <= 20)
    {
        if (!IsOnCooldown(hammer_of_wrath_spell))
        {
            hardcast(hammer_of_wrath_spell);
            return;
        }
    }

    if (!IsOnCooldown(hammer_of_the_righteous_spell))
    {
        hardcast(hammer_of_the_righteous_spell);
        return;
    }

    if (!findAuraOnOurself(divine_plea_spell) && !IsOnCooldown(divine_plea_spell))
    {
        hardcast(divine_plea_spell);
        return;
    }

    if (!findAuraOnOurself(holy_shield_spell) && !IsOnCooldown(holy_shield_spell))
    {
        hardcast(holy_shield_spell);
        return;
    }

    if (!IsOnCooldown(consecration_spell) && distance <= 10)
    {
        hardcast(consecration_spell);
        return;
    }

    if (!IsOnCooldown(avengers_shield_spell))
    {
        hardcast(avengers_shield_spell);
        return;
    }

    if (!IsOnCooldown(shield_of_righteousness_spell))
    {
        hardcast(shield_of_righteousness_spell);
        return;
    }
}


void PaladinRetRotation()
{
    auto current_target = TargetGuid();
    GameObject current_targetGO{};
    auto found_target = findGameObjectByGUID(current_target, current_targetGO);

    if (!findAuraOnOurself(seal_of_command_spell))
    {
        hardcast(seal_of_command_spell);
        return;
    }

    if (!findAuraOnOurself(blessing_of_might) && !findAuraOnOurself(greater_blessing_of_might))
    {
        hardcast(blessing_of_might);
        return;
    }

    if (!InCombat()) return;

    auto distance = ourself.position.Distance(current_targetGO.position);

    if (current_targetGO.health_pct <= 20)
    {
        if (!IsOnCooldown(hammer_of_wrath_spell))
        {
            hardcast(hammer_of_wrath_spell);
            return;
        }
    }

    if (findAuraOnOurself(art_of_war_spell))
    {
        if (ourself.health_pct < 70 && !findAuraOnOurself(divine_plea_spell))
        {
            hardcast(flash_of_light_spell);
            return;
        }

        if (!IsOnCooldown(exorcism_spell))
        {
            hardcast(exorcism_spell);
            return;
        }
    }

    if (distance > 10)
    {
        
        return;
    }

    if (!IsOnCooldown(consecration_spell))
    {
        hardcast(consecration_spell);
    }

    if (!IsOnCooldown(divine_plea_spell))
    {
        if (manapower_percent() < 40)
        {
            hardcast(divine_plea_spell);
        }
    }

    if (!IsOnCooldown(judgement_of_light_spell) && !IsOnCooldown(judgement_of_wisdom_spell) && !IsOnCooldown(
            judgement_of_justice))
    {
        if (findAura(current_targetGO, judgement_of_light_spell, true))
        {
            hardcast(judgement_of_light_spell);
            return;
        }
        else if (findAura(current_targetGO, judgement_of_wisdom_spell, true))
        {
            hardcast(judgement_of_wisdom_spell);
            return;
        }
        else if (!findAura(current_targetGO, judgement_of_light_spell))
        {
            hardcast(judgement_of_light_spell);
            return;
        }
        else if (!findAura(current_targetGO, judgement_of_wisdom_spell))
        {
            hardcast(judgement_of_wisdom_spell);
            return;
        }
        else
        {
            hardcast(judgement_of_justice);
            return;
        }
    }

    if (!IsOnCooldown(crusader_strike_spell))
    {
        hardcast(crusader_strike_spell);
    }

    if (!IsOnCooldown(divine_storm_spell))
    {
        hardcast(divine_storm_spell);
    }
}

void PaladinTankRotationAscension()
{
    auto current_target = TargetGuid();
    GameObject current_targetGO{};
    auto found_target = findGameObjectByGUID(current_target, current_targetGO);

    if (!findAuraOnOurself(seal_of_command_spell))
    {
        hardcast(seal_of_command_spell);
        return;
    }

    if (!findAuraOnOurself(blessing_of_sanctuary_spell) && !findAuraOnOurself(greater_blessing_of_sanctuary_spell) && !
        findAuraOnOurself(sanct_blessing_of_sanctuary))
    {
        hardcast(blessing_of_sanctuary_spell);
        return;
    }

    if (!findAuraOnOurself(righteous_fury))
    {
        hardcast(righteous_fury);
        return;
    }

    if (!InCombat()) return;

    auto nearby_npcs = GetNPCsNotTargetingMe();

    
    if (alternate_taunt)
    {
        alternate_taunt = false;
        for (const auto &entry: nearby_npcs)
        {
            if (CheckSpell(hand_of_reckoning_spell))
            {
                cast_mouseover(hand_of_reckoning_spell, entry.monster.guid);
                return;
            }

            if (CheckSpell(righteous_defense_spell))
            {
                cast_mouseover(righteous_defense_spell, entry.target_of_target.guid);
                return;
            }
        }
    }
    alternate_taunt = true;

    auto distance = ourself.position.Distance(current_targetGO.position);

    if (CheckSpell(divine_plea_spell))
    {
        hardcast(divine_plea_spell);
        return;
    }

    if (!findAuraOnOurself(sacred_shield_spell))
    {
        hardcast(sacred_shield_spell);
        return;
    }

    if (!IsOnCooldown(judgement_of_light_spell) && !IsOnCooldown(judgement_of_wisdom_spell) && !IsOnCooldown(
            judgement_of_justice))
    {
        if (findAura(current_targetGO, judgement_of_light_spell, true))
        {
            hardcast(judgement_of_light_spell);
            return;
        }
        else if (findAura(current_targetGO, judgement_of_wisdom_spell, true))
        {
            hardcast(judgement_of_wisdom_spell);
            return;
        }
        else if (!findAura(current_targetGO, judgement_of_light_spell))
        {
            hardcast(judgement_of_light_spell);
            return;
        }
        else if (!findAura(current_targetGO, judgement_of_wisdom_spell))
        {
            hardcast(judgement_of_wisdom_spell);
            return;
        }
        else
        {
            hardcast(judgement_of_justice);
            return;
        }
    }

    if (distance > 15)
    {
        return;
    }

    if (CheckSpell(consecration_spell))
    {
        hardcast(consecration_spell);
        return;
    }

    if (CheckSpell(holy_shield_spell) && !findAuraOnOurself(holy_shield_spell))
    {
        hardcast(holy_shield_spell);
        return;
    }

    if (CheckSpell(hammer_of_the_righteous_spell))
    {
        hardcast(hammer_of_the_righteous_spell);
        return;
    }

    if (CheckSpell(divine_sacrifice_spell))
    {
        if (const auto group_stuff = groupmembers(70).size(); group_stuff > 3)
        {
            hardcast(divine_sacrifice_spell);
            return;
        }
    }

    if (CheckSpell(shield_of_righteousness_spell))
    {
        hardcast(shield_of_righteousness_spell);
        return;
    }

    if (CheckSpell(avengers_shield_spell))
    {
        hardcast(avengers_shield_spell);
        return;
    }

    if (CheckSpell(divine_storm_spell))
    {
        hardcast(divine_storm_spell);
        return;
    }

    if (CheckSpell(holy_wrath_spell))
    {
        hardcast(holy_wrath_spell);
        return;
    }

    if (CheckSpell(lights_hammer))
    {
        send_key("0");
        return;
    }

    if (CheckSpell(hammerstorm))
    {
        hardcast(hammerstorm);
        return;
    }

    if (CheckSpell(divine_storm_spell))
    {
        hardcast(divine_storm_spell);
        return;
    }

    if (CheckSpell(holy_wrath_spell))
    {
        hardcast(holy_wrath_spell);
        return;
    }
}


static_spell sov("Seal of Vengeance");
static_spell hv_debuff("Holy Vengeance");
static_spell soc("Seal of Command");

void PaladinRetRotationAscension()
{
    auto current_target = TargetGuid();
    GameObject current_targetGO{};
    auto found_target = findGameObjectByGUID(current_target, current_targetGO);

    if (!findAuraOnOurself(blessing_of_might) && !findAuraOnOurself(greater_blessing_of_might) && !
        findAuraOnOurself(battle_shout_spell) && !findAuraOnOurself(sanct_blessing_of_might) && !
        findAuraOnOurself(sanct_blessing_of_kings) && !findAuraOnOurself(sanct_blessing_of_wisdom))
    {
        hardcast(blessing_of_might);
        return;
    }

    if (!InCombat()) return;

    auto distance = ourself.position.Distance(current_targetGO.position);

    if (CheckSpell(divine_plea_spell))
    {
        if (manapower_percent() < 40)
        {
            hardcast(divine_plea_spell);
            return;
        }
    }

    auto twist = CheckSpell(soc) && CheckSpell(sov);
    auto veng_stacks = findAuraMax(current_targetGO, hv_debuff, true);

    
    if (veng_stacks.good && veng_stacks.stack_count == 5 && veng_stacks.time_left > 3)
    {
        if (twist && !findAuraOnOurself(soc))
        {
            hardcast(soc);
            return;
        }
    }

    
    if (!veng_stacks.good || veng_stacks.time_left <= 3)
    {
        if (twist && !findAuraOnOurself(sov))
        {
            hardcast(sov);
            return;
        }
    }

    
    if (veng_stacks.good && veng_stacks.stack_count == 5 && veng_stacks.time_left <= 3)
    if (twist && !findAuraOnOurself(sov) )
    {
        hardcast(sov);
        return;
    }

    if (CheckSpell(judgement_of_light_spell) && CheckSpell(judgement_of_wisdom_spell) && CheckSpell(
            judgement_of_justice))
    {
        if (findAura(current_targetGO, judgement_of_light_spell, true))
        {
            hardcast(judgement_of_light_spell);
            return;
        }
        else if (findAura(current_targetGO, judgement_of_wisdom_spell, true))
        {
            hardcast(judgement_of_wisdom_spell);
            return;
        }
        else if (!findAura(current_targetGO, judgement_of_light_spell))
        {
            hardcast(judgement_of_light_spell);
            return;
        }
        else if (!findAura(current_targetGO, judgement_of_wisdom_spell))
        {
            hardcast(judgement_of_wisdom_spell);
            return;
        }
        else
        {
            hardcast(judgement_of_justice);
            return;
        }
    }

    if (CheckSpell(crusader_strike_spell))
    {
        hardcast(crusader_strike_spell);
        return;
    }

    if (CheckSpell(divine_storm_spell))
    {
        hardcast(divine_storm_spell);
        return;
    }

    static_spell holy_power("Holy Power");
    static_spell templars_verdict("Templar's Verdict");
    auto holypower_info = findAuraMax(ourself, holy_power);

    if (holypower_info.good)
    {
        if (holypower_info.stack_count >= 3)
        {
            hardcast(templars_verdict);
            return;
        }
    }

    if (current_targetGO.health_pct <= 20)
    {
        if (!IsOnCooldown(hammer_of_wrath_spell))
        {
            hardcast(hammer_of_wrath_spell);
            return;
        }
    }

    if (findAuraOnOurself(art_of_war_spell))
    {
        if (ourself.health_pct < 70 && !findAuraOnOurself(divine_plea_spell))
        {
            hardcast(flash_of_light_spell);
            return;
        }

        if (CheckSpell(exorcism_spell))
        {
            hardcast(exorcism_spell);
            return;
        }
    }

    if (CheckSpell(divine_storm_spell))
    {
        hardcast(divine_storm_spell);
    }

    if (CheckSpell(consecration_spell))
    {
        hardcast(consecration_spell);
        return;
    }

    if (CheckSpell(holy_wrath_spell))
    {
        hardcast(holy_wrath_spell);
        return;
    }

    if (CheckSpell(lights_hammer))
    {
        cast_spell_at_feet(lights_hammer);
        return;
    }

    if (CheckSpell(hammerstorm))
    {
        hardcast(hammerstorm);
        return;
    }
}

GameObject findPet()
{
    auto read = read_memory<ulong>(PetGUID);

    GameObject petGO{};
    if (!findGameObjectByGUID(read, petGO))
    {
        return petGO;
    }

    return {};
}

bool hunter_pet_check(GameObject &petGO)
{
    if (petGO.guid == 0) return false;

    auto pet_health = read_memory<int>(petGO.descriptor + 0x18 * 4);
    if (findAura(petGO, mend_pet_spell))
    {
        return false;
    }
    auto pet_max_hp = read_memory<int>(petGO.descriptor + 0x20 * 4);
    auto pet_hp_percent = percent(pet_health, pet_max_hp);

    if (pet_hp_percent <= 98)
    {
        hardcast(mend_pet_spell);
        return true;
    }

    return false;
}

std::vector hunter_key_presses = {"2", "3", "5", "6", "7", "8", "9", "0", "minus"};

void HunterRotation()
{
    auto pet = findPet();
    if (hunter_pet_check(pet)) return;

    if (!findAuraOnOurself(aspect_of_the_viper_spell) && !findAuraOnOurself(aspect_of_the_dragonhawk_spell))
    {
        hardcast(aspect_of_the_dragonhawk_spell);
    }

    if (!findAuraOnOurself(trueshot_aura))
    {
        hardcast(trueshot_aura);
    }

    auto current_target_guid = TargetGuid();
    GameObject current_targetGO{};
    if (!findGameObjectByGUID(current_target_guid, current_targetGO))
    {
        return;
    }

    if (current_target_guid != 0)
    {
        if (!findAura(current_targetGO, serpent_sting_spell, true))
        {
            hardcast(serpent_sting_spell);
        }

        if (!findAura(current_targetGO, hunters_mark_spell))
        {
            hardcast(hunters_mark_spell);
        }
    }

    std::shuffle(std::begin(hunter_key_presses), std::end(hunter_key_presses), rng);
    for (const auto &keypress: hunter_key_presses)
    {
        send_key(keypress);
    }
}

void ascension_hunter_rotation()
{
    auto current_target_guid = TargetGuid();
    GameObject current_targetGO{};

    auto pet = findPet();
    if (hunter_pet_check(pet)) return;

    if (!findGameObjectByGUID(current_target_guid, current_targetGO))
    {
        return;
    }

    auto distance = current_targetGO.position.Distance(ourself.position);

    if (distance <= 5)
    {
        if (player_has_spell(mongoose_bite_spell) && !IsOnCooldown(mongoose_bite_spell))
        {
            hardcast(mongoose_bite_spell);
            return;
        }

        
        hardcast(raptor_strike_spell);
        return;
    }

    if (TargetOfTarget(current_targetGO) == ourself.guid)
    {
        if (!IsOnCooldown(concussive_shot_spell))
        {
            hardcast(concussive_shot_spell);
            return;
        }
    }

    if (!findAura(current_targetGO, hunters_mark_spell))
    {
        hardcast(hunters_mark_spell);
        return;
    }

    if (player_has_spell(multishot_spell) && !IsOnCooldown(multishot_spell))
    {
        hardcast(multishot_spell);
        return;
    }

    if (player_has_spell(kill_command_spell) && !IsOnCooldown(kill_command_spell))
    {
        hardcast(kill_command_spell);
        return;
    }

    if (!findAura(current_targetGO, serpent_sting_spell, true))
    {
        hardcast(serpent_sting_spell);
        return;
    }

    if (player_has_spell(arcane_shot_spell) && !IsOnCooldown(arcane_shot_spell))
    {
        hardcast(arcane_shot_spell);
        return;
    }
}

void FuryRotation()
{
    auto current_target_guid = TargetGuid();
    GameObject current_targetGO{};
    if (!findGameObjectByGUID(current_target_guid, current_targetGO, notype))
    {
        return;
    }

    auto distance = ourself.position.Distance(current_targetGO.position);
    if (distance > 10)
    {
        hardcast(intercept_spell);
    }

    if (!findAuraOnOurself(commanding_shout_spell))
    {
        hardcast(commanding_shout_spell);
    }

    if (findAuraOnOurself(slam_aura))
    {
        hardcast(slam_spell_casted);
    }

    if (ragepower() >= 60)
    {
        hardcast(heroic_strike_spell);
    }

    if (distance < 10)
    {
        if (ragepower() < 40)
        {
            hardcast(bloodrage_spell);
        }
        hardcast(whirlwind_spell);
    }

    hardcast(bloodthirst_spell);
    hardcast(victory_rush_spell);
    hardcast(deathwish_spell);
    hardcast(recklessness_spell);
    hardcast(bloodrage_spell);
}

void ArmsRotation()
{
    auto current_target_guid = TargetGuid();
    GameObject current_targetGO{};
    if (!findGameObjectByGUID(current_target_guid, current_targetGO))
    {
        return;
    }

    auto current_rage = ragepower();

    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_self_buff).count() >= 30 && current_rage > 10)
    {
        if (!findAuraOnOurself(battle_shout_spell))
        {
            hardcast(battle_shout_spell);
        }
        last_self_buff = now;
    }

    if (disengage_because_flags(current_targetGO))
    {
        return;
    }

    if (findAuraOnOurself(berserker_stance))
    {
        auto aoe_mode = (CountNPCTargetingPlayers() >= 2);

        if (aoe_mode)
        {
            if (current_rage > 45)
            {
                hardcast(cleave_spell);
            }

            hardcast(whirlwind_spell);
        }
        else
        {
            if (current_rage >= 42)
            {
                hardcast(heroic_strike_spell);
            }

            hardcast(mortal_strike_spell);
        }

        return;
    }

    if (ourself.position.Distance(current_targetGO.position) > 8 && !InCombat())
    {
        hardcast(charge_spell);
        return;
    }

    if (!findAura(current_targetGO, rend_spell, true))
    {
        hardcast(rend_spell);
        return;
    }

    hardcast(execute_spell);
    hardcast(overpower_spell);

    RandomRotation();
}

std::chrono::time_point last_starfire = std::chrono::steady_clock::now();
std::chrono::time_point last_wrath = std::chrono::steady_clock::now();

void BalanceDruidRotation()
{
    auto current_target_guid = TargetGuid();
    GameObject current_targetGO{};

    if (!findGameObjectByGUID(current_target_guid, current_targetGO))
    {
        logWarn("target not found");
        return;
    }

    if (disengage_because_flags(current_targetGO))
    {
        return;
    }

    if (!IsOnCooldown(starfall_spell))
    {
        hardcast(starfall_spell);
        return;
    }

    if (!findAura(current_targetGO, faerie_fire_spell) && !findAura(current_targetGO, faerie_fire_feral_spell) && !
        IsOnCooldown(faerie_fire_spell))
    {
        hardcast(faerie_fire_spell);
        logWarn("waiting for faeriefire");
        return;
    }

    if (!findAura(current_targetGO, moonfire_spell, true))
    {
        hardcast(moonfire_spell);
        logWarn("waiting for moonfire");
        return;
    }

    if (!findAura(current_targetGO, insect_swarm_spell, true))
    {
        hardcast(insect_swarm_spell);
        logWarn("waiting for insect swarm");

        return;
    }

    if (findAuraOnOurself(eclipse_lunar_aura))
    {
        last_starfire = std::chrono::steady_clock::now();
        hardcast(starfire_spell);
        logWarn("cast return starfire");

        return;
    }

    if (findAuraOnOurself(eclipse_solar_aura))
    {
        last_wrath = std::chrono::steady_clock::now();
        hardcast(wrath_spell);
        logWarn("cast return wrath");

        return;
    }

    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_wrath).count() >= 30)
    {
        logWarn("cd starfire");
        hardcast(starfire_spell);
        return;
    }

    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_starfire).count() >= 30)
    {
        logWarn("cd wrath");
        hardcast(wrath_spell);
        return;
    }

    logWarn("default wrath");
    hardcast(wrath_spell);
}

std::chrono::time_point last_tracking_hack = std::chrono::steady_clock::now();

void tracking_hack_logic()
{
    
    
    auto resource_tracking_flags = read_memory<int>(ourself.descriptor + 0xFFC);
    if (resource_tracking_flags == -1) return;
    auto inworld = is_player_in_world();
    if (!inworld)
    {
        last_tracking_hack = now + std::chrono::seconds(6);
        tracking_hack_enabled = false;
    }

    auto last_track = std::chrono::duration_cast<std::chrono::seconds>(now - last_tracking_hack).count();
    if (last_track >= 4 || !inworld || !tracking_hack_enabled)
    {
        last_tracking_hack = now;
        int zero_write = 0;
        write_memory(ourself.descriptor + 0xFFC, zero_write);
    }
    else
    {
        if (last_track >= 1)
        {
            write_memory(ourself.descriptor + 0xFFC, 2097151);
        }
    }
}


int last_current_cast = 0;

void read_current_casts()
{
    
    if (wow_timestamp - last_current_cast >= 4000) 
    {
        last_spell_out = 0;
        current_channel = 0;
        current_cast = 0;
    }

    auto read = read_memory<int>(ourself.address + 0xA80);
    if (read != 0)
    {
        if (last_spell_out != read)
        {
            last_spell_out = read;
            last_current_cast = wow_timestamp;
        }
    }

    current_channel = read;

    read = 0;
    read = read_memory<int>(ourself.address + 0xA6C);
    if (read != 0)
    {
        if (last_spell_out != read)
        {
            last_spell_out = read;
            last_current_cast = wow_timestamp;
        }
    }
    current_cast = read;
}

void HidePlayerModel()
{
    
    const uint32_t pDescriptor = ourself.descriptor;

    if (pDescriptor == 0)
    {
        return;
    }

    
    const uint32_t display_flags_address = pDescriptor + 0xF0;

    

    
    uint32_t current_packed_value = read_memory<uint32_t>(display_flags_address);

    
    
    const uint32_t upper_bytes = current_packed_value & 0xFFFFFF00;

    
    uint8_t low_byte_flags = current_packed_value & 0xFF;

    
    low_byte_flags |= static_cast<uint8_t>(GameObjectDynamicLowFlags::GO_DYNFLAG_LO_HIDE_MODEL);

    
    uint32_t new_packed_value = upper_bytes | low_byte_flags;

    
    if (new_packed_value != current_packed_value)
    {
        write_memory(display_flags_address, new_packed_value);
        fmt::print("  -> Wrote new packed value 0x{:X} to hide player model.\n", new_packed_value);
    }
    else
    {
        fmt::print("  -> HIDE_MODEL bit was already set in the low byte.\n");
    }
}

void ShowPlayerModel()
{
    const uint32_t pDescriptor = ourself.descriptor;
    if (pDescriptor == 0) return;

    const uint32_t display_flags_address = pDescriptor + 0xF0;
    uint32_t current_packed_value = read_memory<uint32_t>(display_flags_address);
    uint8_t low_byte_flags = current_packed_value & 0xFF;

    
    low_byte_flags &= ~static_cast<uint8_t>(GameObjectDynamicLowFlags::GO_DYNFLAG_LO_HIDE_MODEL);

    uint32_t new_packed_value = (current_packed_value & 0xFFFFFF00) | low_byte_flags;

    if (new_packed_value != current_packed_value)
    {
        write_memory(display_flags_address, new_packed_value);
    }
}

void MacroDesync()
{
    
    uint32_t addr_SyncConfig = 0x00C7BFFC;
    
    uint32_t addr_SyncBindings = 0x00C7BFF8;

    auto all_enable = read_memory<int>(addr_SyncConfig);
    auto sync_macros_bindings = read_memory<int>(addr_SyncBindings);

    if (!all_enable && !sync_macros_bindings) return;

    int disable = 0;

    write_memory(addr_SyncConfig, disable);
    write_memory(addr_SyncBindings, disable);
}

bool memory_setup()
{
    if (wow_pid == 0)
    {
        return false;
    }

    GetWowTimestampCount();
    RescanGameObjectCache();
    FindPlayerPointer();

    if (ourself.address <= 0)
    {
        return false;
    }

    

    if (resolved_player_guid < 1)
    {
        logWarn("Player guid misread");
        resetPlayerPtr();
        return false;
    }

    read_class();
    if (resolved_player_class == 0)
    {
        return false;
    }

    CacheSpellsDBC();

    MacroDesync();
    read_original_actionbar_spells();
    tracking_hack_logic();
    read_current_casts();
    

    

    
    
    

    return true;
}

bool SelfBuffHealer()
{
    GameObject main_tank{};
    switch (resolved_player_class)
    {
        case Paladin:
            {
                if (!findAuraOnOurself(seal_of_wisdom_spell))
                {
                    hardcast(seal_of_wisdom_spell);
                    return true;
                }

                if (!findAuraOnOurself(blessing_of_wisdom_spell) && !
                    findAuraOnOurself(greater_blessing_of_wisdom_spell))
                {
                    modify_castbar_and_cast_self(blessing_of_wisdom_spell);
                    return true;
                }

                if (!findGameObjectByGUID(main_tank_guid, main_tank, Player)) return false;

                if (main_tank.health <= 0) return false;
                if (main_tank.position.Distance(ourself.position) > 30) return false;

                if (!findAura(main_tank, beacon_of_light_spell, true))
                {
                    castHeal(beacon_of_light_spell, main_tank_guid);
                    return true;
                }

                if (!InCombat()) return false;

                GameObject target_of_target{};
                auto targoftank = TargetOfTarget(main_tank);
                if (targoftank && findGameObjectByGUID(targoftank, target_of_target, Unit))
                {
                    if (disengage_because_flags(target_of_target)) return false;
                    if (main_tank.position.Distance(target_of_target.position) >= 8) return false;
                    if (IsOnCooldown(judgement_of_light_spell) || IsOnCooldown(judgement_of_wisdom_spell) ||
                        IsOnCooldown(judgement_of_justice)) return false;
                    if (target_of_target.health_pct >= 99) return false; 
                    int judged_light = findAuraTimeLeft(target_of_target, judgement_of_light_spell, true);
                    int needs_refresh = findAuraTimeLeft(ourself, judgements_of_the_pure_aura);

                    
                    if (judged_light <= 4 || needs_refresh <= 1)
                    {
                        castHeal(judgement_of_light_spell, targoftank);
                        send_key("t");
                        return true;
                    }
                }

                return false;
            }
        case Priest:
            {
                if (!findAuraOnOurself(inner_fire_spell))
                {
                    hardcast(inner_fire_spell);
                    return true;
                }

                return false;
            }
        case Shaman:
            {
                if (!findAuraOnOurself(water_shield_spell))
                {
                    hardcast(water_shield_spell);
                    return true;
                }

                if (findGameObjectByGUID(main_tank_guid, main_tank, Player))
                {
                    if (main_tank.health <= 0) return false;
                    if (main_tank.position.Distance(ourself.position) > 30) return false;

                    if (!findAura(main_tank, earth_shield_spell))
                    {
                        castHeal(earth_shield_spell, main_tank_guid);
                        return true;
                    }
                }
                return false;
            }
        case Druid:
            {
                
                return false;
            }
        default:
            {
                return false;
            }
    }

    return false;
}

void dump_flags(const GameObject &target1)
{
    auto flags = ReadFlagsForGO(target1);

    std::vector<std::string> flags_to_log;

    for (const auto &[flag_value, flag_name]: flag_names)
    {
        if (flags & flag_value)
        {
            flags_to_log.push_back(flag_name);
        }
    }

    if (!flags_to_log.empty())
    {
        fmt::println("(Dump) UnitFlags: {}", fmt::join(flags_to_log, ", "));
    }
}

int FriendlyTargetsNearby(GameObject &targetGO, std::vector<GameObject> &vector)
{
    int retval = 0;

    for (const auto &entry: vector)
    {
        if (targetGO.position.Distance(entry.position) > 8)
        {
            continue;
        }
        retval += 1;
    }

    return retval;
}

bool dispell_decurse_removepoisons(GameObject &entry)
{
    if (resolved_player_class == Druid)
    {
        const bool curse_present = findAura(entry, gehennascurse) || findAura(entry, lucifroncurse) ||
                                   findAura(entry, ancientdispaircurse) || findAura(entry, ancientfurycurse) ||
                                   findAura(entry, ancienthysteriacurse) || findAura(entry, veilofshadowcurse) ||
                                   findAura(entry, ancientdreadcurse) || findAura(entry, shrinkcurse) ||
                                   findAura(entry, curseofblood) || findAura(entry, curseofvulnerabilitycurse) ||
                                   findAura(entry, curseoftonguescurse) || findAura(entry, callofthegrave);
        if (curse_present)
        {
            castHeal(remove_curse_spell, entry.guid);
            return true;
        }

        const bool poison_present = findAura(entry, mortalpoison);

        if (poison_present)
        {
            if (!findAura(entry, abolish_poison))
            {
                castHeal(abolish_poison, entry.guid);
                return true;
            }
        }
    }

    return false;
}

inline bool sort_final_healer(const GameObject &go1, const GameObject &go2)
{
    if (go1.priority != go2.priority)
    {
        return go1.priority;
    }

    return (go1.health_pct < go2.health_pct);
}

bool HealerRotationLogic(std::vector<GameObject> &list)
{
    bool prayer_of_mending_isout = false;
    bool redemption = false;
    bool clearcasting = false;
    bool tree_form = false;

    
    bool shield_cooldown = false;
    bool prayer_of_mending_cooldown = false;
    bool lay_on_hands_cooldown = false;
    bool holy_shock_cooldown = false;
    int raid_count = ReadRaidCount();

    if (resolved_player_class == Paladin)
    {
        lay_on_hands_cooldown = IsOnCooldown(lay_on_hands_spell);
        holy_shock_cooldown = IsOnCooldown(holy_shock_spell);
    }

    if (resolved_player_class == Priest)
    {
        prayer_of_mending_isout = FindAuraByParty(list, prayer_of_mending_spell, true);
        redemption = findAuraOnOurself(spirit_of_redemption);
        shield_cooldown = IsOnCooldown(power_word_shield_spell);
        prayer_of_mending_cooldown = IsOnCooldown(prayer_of_mending_spell);
    }

    if (resolved_player_class == Druid)
    {
        clearcasting = findAuraOnOurself(clearcasting_spell);
        tree_form = findAuraOnOurself(tree_of_life_form, true);
    }

    bool riptide_cooldown = false;
    if (resolved_player_class == Shaman)
    {
        riptide_cooldown = IsOnCooldown(riptide_spell);
    }

    
    int num_needs_heal = 0;
    for (auto &entry: list)
    {
        if (entry.health_pct <= 98) num_needs_heal += 1;
        auto num_targeting = CountNPCTargetingPlayer(entry.guid);
        if (num_targeting >= 1)
        {
            entry.priority = true;
        }

        if (findAura(entry, burn_through_spell, false))
        {
            entry.priority = true;
            entry.health_pct = 1;
        }

        if (entry.priority)
            if (resolved_player_class == Druid)
            {
                if (dispell_decurse_removepoisons(entry)) return true;
            }
    }

    std::sort(list.begin(), list.end(), sort_final_healer);

    for (auto &entry: list)
    {
        
        if (player_currently_channeling() != 0) 
        {
            return true;
        }

        if (entry.guid == 0)
        {
            continue;
        }
        if (entry.health_pct == 0)
        {
            continue;
        }

        auto flags = ReadFlagsForGO(entry);

        if (flags & UNIT_FLAG_NON_ATTACKABLE)
        {
            continue;
        }

        if (flags & UNIT_FLAG_POSSESSED)
        {
            continue;
        }

        if (flags & UNIT_FLAG_IMMUNE_TO_PC)
        {
            continue;
        }

        if (flags & UNIT_FLAG_NOT_SELECTABLE)
        {
            continue;
        }

        
        auto relations = getUnitRelationship(ourself.address, entry.address);
        if (relations != Relationship::Friendly)
        {
            continue;
        }

        switch (resolved_player_class)
        {
            case Paladin:
                {
                    if (entry.guid == main_tank_guid && entry.health_pct > 50 && findAura(
                            entry, beacon_of_light_spell, true) && num_needs_heal >= 2)
                    {
                        continue;
                    }

                    if (entry.health_pct <= 25)
                    {
                        if (!lay_on_hands_cooldown)
                        {
                            castHeal(lay_on_hands_spell, entry.guid);
                            return true;
                        }
                    }

                    if (entry.health_pct < 70)
                    {
                        if (!holy_shock_cooldown)
                        {
                            castHeal(holy_shock_spell, entry.guid);
                            return true;
                        }
                    }

                    if (entry.health_pct <= 98)
                    {
                        castHeal(holy_light_spell, entry.guid);
                        return true;
                    }
                }
            case Priest:
                {
                    if (CheckSpell(halo_spell))
                    {
                        bool halo_mode = false;
                        
                        if (raid_count >= 5 && num_needs_heal == raid_count) halo_mode = true;
                        if (raid_count == 0 && num_needs_heal >= 3) halo_mode = true;

                        if (halo_mode)
                        {
                            if (CheckSpell(inner_focus_spell) && !findAuraOnOurself(inner_focus_spell))
                            {
                                hardcast(inner_focus_spell);
                                return true;
                            }
                            if (findAuraOnOurself(inner_focus_spell))
                            {
                                hardcast(halo_spell);
                                return true;
                            }
                        }
                    }

                    auto shielded = findAura(entry, weakened_soul_aura) || findAura(entry, power_word_shield_spell);
                    if (!redemption)
                    {
                        if (!shielded && !shield_cooldown) 
                        {
                            
                            {
                                castHeal(power_word_shield_spell, entry.guid);
                                return true;
                            }
                        }
                    }

                    if (entry.health_pct == 100 && !entry.priority) continue;

                    if (entry.health_pct >= 78)
                        if (!findAura(entry, renew_spell, true))
                        {
                            castHeal(renew_spell, entry.guid);
                            return true;
                        }

                    if (entry.health_pct >= 78) continue;

                    if (CheckSpell(pain_supression) && !redemption && entry.priority && entry.health_pct <= 50)
                    {
                        castHeal(pain_supression, entry.guid);
                        return true;
                    }

                    
                    
                    
                    
                    
                    
                    
                    
                    if (entry.health_pct <= 85)
                    {
                        if (CheckSpell(penance))
                        {
                            if (CheckSpell(inner_focus_spell) && !findAuraOnOurself(inner_focus_spell))
                            {
                                hardcast(inner_focus_spell);
                                return true;
                            }

                            castHeal(penance, entry.guid);
                            return true;
                        }
                    }
                    

                    if (CheckSpell(prayer_of_mending_spell))
                        if (!prayer_of_mending_cooldown && !prayer_of_mending_isout && entry.priority)
                        {
                            castHeal(prayer_of_mending_spell, entry.guid);
                            return true;
                        }

                    
                    if (CheckSpell(prayer_of_healing_spell))
                    {
                        if (num_needs_heal > 4)
                        {
                            
                            
                        }
                    }

                    if (CheckSpell(flash_heal_spell))
                    {
                        castHeal(flash_heal_spell, entry.guid);
                        return true;
                    }

                    castHeal(priest_lesser_heal_spell, entry.guid);
                    return true;
                }
            case Shaman:
                {
                    auto friendly_targets_nearby = FriendlyTargetsNearby(entry, list);

                    if (entry.health_pct <= 25)
                    {
                        hardcast(natures_swiftness_spell);
                    }

                    if (!riptide_cooldown && !findAura(entry, riptide_spell, true))
                    {
                        castHeal(riptide_spell, entry.guid);
                        return true;
                    }

                    if (friendly_targets_nearby >= 2)
                    {
                        castHeal(chain_heal_spell, entry.guid);
                    }
                    else
                    {
                        castHeal(healing_wave_spell, entry.guid);
                    }
                }
            case Druid:
                {
                    if (entry.health_pct == 100 && !entry.priority) continue;
                    auto rejuv = findAuraTimeLeft(entry, rejuvenation_spell, true);
                    auto regrowth = findAuraTimeLeft(entry, regrowth_spell, true);
                    bool nourish_mode = false;
                    auto aoe_heal_needed = (FriendlyTargetsNearby(entry, list) >= 2);

                    if (aoe_heal_needed && CheckSpell(wild_growth_spell))
                    {
                        castHeal(wild_growth_spell, entry.guid);
                        logWarn("Wild growrth return");
                        return true;
                    }

                    if (entry.health_pct <= 90)
                    {
                        if (CheckSpell(nourish_spell))
                        {
                            if ((rejuv && regrowth && findAuraTimeLeft(entry, wild_growth_spell, true) ||
                                 findAuraTimeLeft(entry, lifebloom_spell, true)) && entry.health_pct <= 90)
                            {
                                nourish_mode = true;
                            }
                        }
                    }

                    if (entry.health_pct <= 90)
                    {
                        if (regrowth || rejuv)
                        {
                            if (CheckSpell(swiftmend_spell))
                            {
                                castHeal(swiftmend_spell, entry.guid);
                                logWarn("Swiftmend return");
                                return true;
                            }
                        }

                        if (CheckSpell(natures_swiftness_spell) && !findAuraOnOurself(natures_swiftness_spell))
                        {
                            hardcast(natures_swiftness_spell);
                            return true;
                        }

                        if (findAuraOnOurself(natures_swiftness_spell) && nourish_mode)
                        {
                            castHeal(nourish_spell, entry.guid);
                            return true;
                        }
                    }

                    
                    if (clearcasting)
                        if (CheckSpell(lifebloom_spell) && findAuraMax(entry, lifebloom_spell, true).stack_count < 1)
                        {
                            logWarn("lifebloom return");
                            castHeal(lifebloom_spell, entry.guid);
                            return true;
                        }

                    if (!rejuv)
                    {
                        logWarn("rejuv return");
                        castHeal(rejuvenation_spell, entry.guid);
                        return true;
                    }

                    if (entry.health_pct <= 99)
                    {
                        
                        if (nourish_mode)
                        {
                            logWarn("nourish return");
                            castHeal(nourish_spell, entry.guid);
                            return true;
                        }
                    }

                    if (entry.health_pct <= 99 && raid_count <= 5)
                    {
                        if (!regrowth)
                        {
                            if (castHeal(regrowth_spell, entry.guid, false))
                            {
                                logWarn("regrowth going out?");
                                return true;
                            }
                        }
                    }

                    
                    if (CheckSpell(lifebloom_spell) && findAuraMax(entry, lifebloom_spell, true).stack_count < 3)
                    {
                        logWarn("lifebloom return");
                        castHeal(lifebloom_spell, entry.guid);
                        return true;
                    }

                    if (entry.health_pct == 100) continue;

                    if (!tree_form)
                    {
                        if (regrowth && entry.health_pct <= 80)
                        {
                            castHeal(healing_touch_spell, entry.guid);
                            return true;
                        }
                    }

                    if (entry.health_pct <= 80 && tree_form && !CheckSpell(nourish_spell))
                    {
                        if (castHeal(regrowth_spell, entry.guid, false))
                        {
                            logWarn("regrowth going out?");
                            return true;
                        }
                    }
                }

            default:
                {
                    return false;
                }
        }
    }

    return false;
}

void main_tank_check(std::vector<GameObject> &members)
{
    if (resolved_player_class == Druid || resolved_player_class == Priest)
    {
        return;
    }

    for (auto &entry: members)
    {
        if (!findAura(entry, earth_shield_spell, true) && !findAura(entry, beacon_of_light_spell, true))
        {
            continue;
        }

        if (main_tank_guid != entry.guid)
        {
            logInfo("Main tank has been switched to {} {}", entry.guid, GetPlayerNameFromGuid(entry.guid));
        }
        main_tank_guid = entry.guid;
        return;
    }
}

bool HealerRotation()
{
    auto grouplist = groupmembers(110); 

    main_tank_check(grouplist);

    
    if (SelfBuffHealer()) return true;

    return HealerRotationLogic(grouplist);
}

void PriestFunserverDps()
{
    if (HealerRotation()) return;
    auto current_target = TargetGuid();
    GameObject current_targetGO{};
    auto found_target = findGameObjectByGUID(current_target, current_targetGO);

    if (CheckSpell(vampiric_embrace) && !findAuraOnOurself(vampiric_embrace))
    {
        hardcast(vampiric_embrace);
        return;
    }

    if (CheckSpell(divine_spirit) && !findAuraOnOurself(divine_spirit))
    {
        hardcast(divine_spirit);
        return;
    }

    if (CheckSpell(inner_fire_spell) && !findAuraOnOurself(inner_fire_spell))
    {
        hardcast(inner_fire_spell);
        return;
    }

    if (solo())
    {
        auto shielded = findAura(ourself, weakened_soul_aura) || findAura(ourself, power_word_shield_spell);

        if (!shielded && !IsOnCooldown(power_word_shield_spell))
        {
            castHeal(power_word_shield_spell, ourself.guid);
            return;
        }
    }

    if (!found_target) return;
    if (disengage_because_flags(current_targetGO)) return;
    if (!isUnitHostile(current_targetGO)) return;

    auto targ_of_targ = TargetOfTarget(current_targetGO);
    if (targ_of_targ != 0 && targ_of_targ != ourself.guid)
    {
        auto aoe_mode = (CountNPCTargetingPlayer(targ_of_targ) >= 2 && current_targetGO.type != Player);

        if (aoe_mode)
        {
            if (player_currently_channeling() != 0) return;

            if (CheckSpell(mind_sear_spell))
            {
                
                
            }
        }
    }

    if (!CheckSpell(mind_flay))
    {
        if (CheckSpell(holy_fire_spell))
        {
            hardcast(holy_fire_spell);
            return;
        }

        if (CheckSpell(smite_spell))
        {
            hardcast(smite_spell);
            return;
        }
    }

    if (!IsOnCooldown(mind_blast))
    {
        hardcast(mind_blast);
        return;
    }

    if (!findAura(current_targetGO, shadow_word_pain, true))
    {
        hardcast(shadow_word_pain);
        return;
    }

    if (!findAura(current_targetGO, devouring_plague_spell, true))
    {
        hardcast(devouring_plague_spell);
        return;
    }

    if (player_currently_channeling() != 0) return;

    hardcast(mind_flay);
}

std::chrono::time_point time_since_last_self_heal = std::chrono::steady_clock::now();

void ShamanDpsRotation()
{
    auto current_target_guid = TargetGuid();
    GameObject current_targetGO{};

    if (player_has_spell(water_shield_spell))
    {
        if (!findAuraOnOurself(water_shield_spell))
        {
            hardcast(water_shield_spell);
            return;
        }
    }
    else
    {
        if (player_has_spell(lightning_shield_spell))
        {
            if (!findAuraOnOurself(lightning_shield_spell))
            {
                hardcast(lightning_shield_spell);
                return;
            }
        }
    }

    if (!findGameObjectByGUID(current_target_guid, current_targetGO))
    {
        return;
    }

    if (player_has_spell(flame_shock_spell))
    {
        auto flameshockcd = IsOnCooldown(flame_shock_spell);
        if (!flameshockcd && !findAura(current_targetGO, flame_shock_spell, true))
        {
            hardcast(flame_shock_spell);
            return;
        }
    }
    else
    {
        auto earthshockcd = IsOnCooldown(earth_shock_spell);
        if (!earthshockcd && findAuraTimeLeft(current_targetGO, earth_shock_spell) < 2)
        {
            hardcast(earth_shock_spell);
            return;
        }
    }

    if (ourself.health_pct <= 60)
    {
        if (std::chrono::duration_cast<std::chrono::seconds>(now - time_since_last_self_heal).count() > 5)
        {
            hardcast(lesser_healing_wave_spell);
            time_since_last_self_heal = now;
            return;
        }
    }

    auto thunderstormCD = IsOnCooldown(thunderstorm_spell);
    auto lavaburstCD = IsOnCooldown(lavaburst_spell);

    if (player_has_spell(lavaburst_spell) && !lavaburstCD)
    {
        if (findAuraTimeLeft(current_targetGO, flame_shock_spell) > 2)
        {
            hardcast(lavaburst_spell);
            return;
        }
    }

    if (player_has_spell(thunderstorm_spell) && !thunderstormCD)
    {
        hardcast(thunderstorm_spell);
        return;
    }

    hardcast(lightning_bolt_spell);
}

void MageDpsRotation()
{
    if (!findAuraOnOurself(molten_armor_spell))
    {
        hardcast(molten_armor_spell);
        return;
    }

    if (!findAuraOnOurself(arcane_intellect_spell))
    {
        hardcast(arcane_intellect_spell);
        return;
    }

    GameObject targetGO{};
    if (!findGameObjectByGUID(TargetGuid(), targetGO))
    {
        return;
    }

    hardcast(fire_blast_spell);

    if (findAuraOnOurself(hot_streak_aura))
    {
        hardcast(combustion_spell);
        hardcast(pyroblast_spell);
        return;
    }

    if (!findAura(targetGO, living_bomb_spell, true))
    {
        hardcast(living_bomb_spell);
    }

    if (!findAura(targetGO, improved_scorch_aura, false))
    {
        if (!hardcast(scorch_spell, false)) return;
    }

    hardcast(fireball_spell);
}

void WarlockDpsRotation()
{
    auto current_target_guid = TargetGuid();
    GameObject current_targetGO{};

    auto read = read_memory<ulong>(PetGUID);

    GameObject petGO{};
    bool pet_found = findGameObjectByGUID(read, petGO);

    if (player_has_spell(fel_armor_spell))
    {
        if (!findAuraOnOurself(fel_armor_spell))
        {
            hardcast(fel_armor_spell);
            return;
        }
    }
    else
    {
        if (!findAuraOnOurself(demon_armor_spell))
        {
            hardcast(demon_armor_spell);
            return;
        }
    }

    if (!findGameObjectByGUID(current_target_guid, current_targetGO))
    {
        return;
    }

    if (findAuraOnOurself(metamorphosis_spell))
    {
        if (!IsOnCooldown(immolation_aura_spell))
        {
            hardcast(immolation_aura_spell);
            return;
        }
    }

    if (InCombat())
    {
        if (pet_found)
        {
            if (petGO.guid && player_has_spell(soul_link) && !findAura(petGO, soul_link))
            {
                hardcast(soul_link);
                return;
            }

            
            
            
            if (player_has_spell(demonic_empowerment_spell))
            {
                if (!IsOnCooldown(demonic_empowerment_spell))
                {
                    hardcast(demonic_empowerment_spell);
                    return;
                }
            }
        }
    }

    if (!findAura(current_targetGO, corruption_spell, true))
    {
        hardcast(corruption_spell);
        return;
    }

    auto targ_of_targ = TargetOfTarget(current_targetGO);
    if (targ_of_targ != resolved_player_guid)
    {
        auto aoe_mode = (CountNPCTargetingPlayer(targ_of_targ) >= 4 && current_targetGO.type != Player);

        if (aoe_mode)
        {
            hardcast(seed_of_corruption_spell);
            return;
        }
    }

    
    auto weakness = findAura(current_targetGO, curse_of_weakness_spell, false);
    auto elements = findAura(current_targetGO, curse_of_elements_spell, false) ||
                    findAura(current_targetGO, earth_and_moon_aura, false) || findAura(
                        current_targetGO, ebon_plaguebringer_spell, false);

    auto one_out = findAura(current_targetGO, curse_of_elements_spell, true) || findAura(
                       current_targetGO, curse_of_weakness_spell, true);

    if (!one_out)
    {
        if (!elements && player_has_spell(curse_of_elements_spell))
        {
            hardcast(curse_of_elements_spell);
            return;
        }
        else if (!weakness && player_has_spell(curse_of_weakness_spell))
        {
            hardcast(curse_of_weakness_spell);
            return;
        }
        else
        {
            if (!findAura(current_targetGO, curse_of_agony_spell, true))
            {
                hardcast(curse_of_agony_spell);
                return;
            }
        }
    }

    if (!findAura(current_targetGO, immolate_spell, true))
    {
        if (!hardcast(immolate_spell, false))
        {
            return;
        }
    }

    if (player_has_spell(soul_fire_spell))
    {
        if (findAuraTimeLeft(ourself, decimation_spell) >= 4)
        {
            hardcast(soul_fire_spell);
            return;
        }
    }

    if (player_has_spell(incinerate_spell))
    {
        if (findAuraOnOurself(molten_core_spell))
        {
            hardcast(incinerate_spell);
            return;
        }
    }

    if (ourself.health_pct >= 90 && manapower_percent() <= 10 || !findAuraOnOurself(life_tap_spell))
    {
        hardcast(life_tap_spell);
        return;
    }

    hardcast(shadowbolt_spell);
}

void ShadowPriestRotation()
{
    GameObject targetGO{};
    if (!findGameObjectByGUID(TargetGuid(), targetGO))
    {
        return; 
    }

    if (!findAuraOnOurself(vampiric_embrace))
    {
        hardcast(vampiric_embrace);
        return;
    }

    if (!findAuraOnOurself(weakened_soul_aura) && !findAuraOnOurself(power_word_shield_spell))
    {
        hardcast(power_word_shield_spell);
        return;
    }

    if (player_has_spell(mind_sear_spell))
    {
        auto targ_of_targ = TargetOfTarget(targetGO);
        if (targ_of_targ != resolved_player_guid)
        {
            auto aoe_mode = (CountNPCTargetingPlayer(targ_of_targ) >= 4 && targetGO.type != Player);

            if (aoe_mode)
            {
                hardcast(mind_sear_spell);
                return;
            }
        }
    }

    if (!findAura(targetGO, shadow_word_pain, true))
    {
        hardcast(shadow_word_pain);
        return;
    }

    if (!findAura(targetGO, vampiric_touch_spell, true))
    {
        if (!hardcast(vampiric_touch_spell, false)) return;
    }

    if (!findAura(targetGO, devouring_plague, true))
    {
        hardcast(devouring_plague);
        return;
    }

    if (player_has_spell(shadow_word_death))
    {
        if (targetGO.health_pct <= 35 && !IsOnCooldown(shadow_word_death))
        {
            hardcast(shadow_word_death);
            return;
        }
    }

    if (!IsOnCooldown(mind_blast))
    {
        hardcast(mind_blast);
        return;
    }

    hardcast(mind_flay);
}

int GetComboPoints()
{
    return static_cast<int>(read_memory<std::byte>(ComboPoints)); 
}

void RogueAssasinationRotation()
{
    if (!InCombat())
    {
        hardcast(stealth_aura);
    }

    if (findAuraOnOurself(stealth_aura))
    {
        hardcast(garrote_spell);
        return; 
    }

    GameObject targetGO{};
    if (!findGameObjectByGUID(TargetGuid(), targetGO))
    {
        return; 
    }

    auto combopoints = GetComboPoints();

    auto energy = energypower();

    auto targ_of_targ = TargetOfTarget(targetGO);

    auto aoe_mode = (CountNPCTargetingPlayer(targ_of_targ) >= 6);

    send_key("t");

    if (aoe_mode)
    {
        hardcast(cold_blood_spell);
        hardcast(fan_of_knives);
        return;
    }

    auto time_left_on_slice_n_dice = findAuraTimeLeft(ourself, slice_and_dice_spell);

    if (combopoints == 0)
    {
        hardcast(mutilate_spell);
    }

    if (combopoints && combopoints < 3)
    {
        if (findAuraTimeLeft(targetGO, expose_armor_spell, false) <= 2)
        {
            hardcast(expose_armor_spell);
            
            return;
        }
    }

    if (combopoints >= 1)
    {
        if (time_left_on_slice_n_dice <= 4 && time_left_on_slice_n_dice != 0)
        {
            hardcast(envenom_spell);

            
            return;
        }

        if (time_left_on_slice_n_dice == 0)
        {
            hardcast(slice_and_dice_spell);

            
            return;
        }
    }

    
    if (!findAuraOnOurself(hunger_for_blood_spell))
    {
        hardcast(hunger_for_blood_spell);

        if (!findAura(targetGO, rupture_spell))
        {
            if (energy >= 15 && combopoints >= 1)
            {
                hardcast(rupture_spell);
                
                return;
            }
        }
    }

    if (combopoints == 5)
    {
        hardcast(cold_blood_spell);

        if (energy <= 75)
        {
            return;
        }

        hardcast(envenom_spell);
        
        return;
    }

    hardcast(mutilate_spell);
}

void RogueCombatRotation()
{
    if (!InCombat())
    {
        hardcast(stealth_aura);
    }

    if (findAuraOnOurself(stealth_aura))
    {
        hardcast(ambush_spell);
        return; 
    }

    GameObject targetGO{};
    if (!findGameObjectByGUID(TargetGuid(), targetGO))
    {
        return; 
    }

    auto combopoints = GetComboPoints();

    auto targ_of_targ = TargetOfTarget(targetGO);

    if (combopoints == 1)
    {
        if (findAuraTimeLeft(targetGO, expose_armor_spell, false) <= 2)
        {
            hardcast(expose_armor_spell);

            return;
        }
    }

    if (combopoints >= 1)
    {
        if (findAuraTimeLeft(ourself, slice_and_dice_spell) <= 1)
        {
            hardcast(slice_and_dice_spell);

            return;
        }
    }

    if (combopoints == 5)
    {
        if (!player_has_spell(rupture_spell))
        {
            hardcast(eviscerate_spell);
        }
        else
        {
            
        }
    }
    else
    {
        hardcast(sinister_strike_spell);
    }
}

void RogueSubRotation()
{
    GameObject targetGO{};
    if (!findGameObjectByGUID(TargetGuid(), targetGO))
    {
        return; 
    }

    auto combopoints = GetComboPoints();

    if (findAuraOnOurself(shadow_dance_aura))
    {
        if (!findAura(targetGO, garrote_aura))
        {
            send_key("2");
            return;
        }

        if (combopoints == 5)
        {
            send_key("4");
            return;
        }

        send_key("3"); 
    }
    if (findAuraOnOurself(stealth_aura))
    {
        return; 
    }

    if (combopoints == 5)
    {
        if (!findAuraOnOurself(slice_and_dice_spell))
        {
            hardcast(slice_and_dice_spell);
            return;
        }

        if (!findAura(targetGO, rupture_spell))
        {
            hardcast(rupture_spell);
            return;
        }

        if (!findAura(targetGO, expose_armor_spell))
        {
            hardcast(expose_armor_spell);
            return;
        }

        hardcast(envenom_spell);
        return;
    }

    if (!findAura(targetGO, hemorrhage_spell))
    {
        hardcast(hemorrhage_spell);
    }

    hardcast(backstab_spell);

    hardcast(hemorrhage_spell);
}

bool backstab_possible(const GameObject &game_object)
{
    auto our_position = ourself.position;
    auto target_position = game_object.position;
    auto our_rotation = get_rotation_from_substructure(ourself.address);
    auto target_rotation = get_rotation_from_substructure(game_object.address);

    
    Point to_us = {our_position.x - target_position.x, 0.0f, our_position.z - target_position.z};
    to_us = to_us.Normalize();

    Point to_target = {target_position.x - our_position.x, 0.0f, target_position.z - our_position.z};
    to_target = to_target.Normalize();

    
    
    Point target_facing = {std::cos(target_rotation), 0.0f, std::sin(target_rotation)};
    Point our_facing = {std::cos(our_rotation), 0.0f, std::sin(our_rotation)};

    
    float alignment_behind = Point::Dot(to_us, target_facing);
    float alignment_facing = Point::Dot(our_facing, to_target);

    
    
    bool behind = alignment_behind < -0.2f;
    bool facing = alignment_facing > 0.5f;

    return behind && facing;
}

#include <fmt/core.h>

void DruidCatRotation(GameObject &targetGO)
{
    if (player_currently_channeling() != 0) return;
    
    
    
    auto energy = energypower();
    auto cp = GetComboPoints();
    auto flags = ReadFlagsForGO(ourself);
    bool is_silenced = (flags & UNIT_FLAG_SILENCED);

    
    static_spell savage_roar_spell("Savage Roar");
    static_spell berserk_spell("Berserk");
    static_spell tigers_fury_spell("Tiger's Fury");
    static_spell clearcasting_spell("Clearcasting");
    static_spell kotj_spell("Incarnation: King of the Jungle");
    static_spell feral_frenzy_spell("Feral Frenzy");

    bool has_savage_roar = findAuraOnOurself(savage_roar_spell, true);
    bool has_berserk = findAuraOnOurself(berserk_spell);
    bool has_tigers_fury = findAuraOnOurself(tigers_fury_spell);
    bool has_clearcasting = findAuraOnOurself(clearcasting_spell);
    bool has_kotj = findAuraOnOurself(kotj_spell);

    
    static_spell ambushpredator("Ambush Predator");
    static_spell pve_mode("PvE Mode");
    static_spell swipe_cat_spell("Swipe (Cat)");
    static_spell faerie_fire_feral_spell("Faerie Fire (Feral)");
    static_spell faerie_fire_spell("Faerie Fire");

    
    static_spell rake_spell("Rake");
    static_spell rip_spell("Rip");
    static_spell mangle_cat_spell("Mangle (Cat)");
    static_spell mangle_bear_spell("Mangle (Bear)");
    static_spell shred_spell("Shred");
    static_spell ravage_spell("Ravage");
    static_spell ferocious_bite_spell("Ferocious Bite");
    static_spell claw_spell("Claw");

    bool has_ambush_proc = findAuraOnOurself(ambushpredator);
    bool is_ascension = player_has_spell(pve_mode);
    bool is_behind = backstab_possible(targetGO);
    int target_hp_pct = targetGO.health_pct;

    
    auto roar_time_left = findAuraTimeLeft(ourself, savage_roar_spell, true);
    if (roar_time_left < 0) roar_time_left = 0;

    auto rake_time_left = findAuraTimeLeft(targetGO, rake_spell, true);
    if (rake_time_left < 0) rake_time_left = 0;

    auto rip_time_left = findAuraTimeLeft(targetGO, rip_spell, true);
    if (rip_time_left < 0) rip_time_left = 0;

    
    bool has_mangle_debuff = (findAuraTimeLeft(targetGO, mangle_cat_spell, false) > 0) || (
                                 findAuraTimeLeft(targetGO, mangle_bear_spell, false) > 0);

    
    bool force_dump = (energy >= 85) || has_berserk;
    bool execute_range = (target_hp_pct > 0 && target_hp_pct <= 25);
    bool bite_refreshes_rip = has_kotj || execute_range;

    
    bool skip_rip = execute_range && (rip_time_left == 0);

    
    auto GetCost = [&](int base) { return has_berserk ? (base / 2) : base; };

    
    constexpr int SAVAGE_ROAR_COST = 25;
    constexpr int RIP_COST = 30;
    constexpr int BITE_COST = 35;
    constexpr int RAKE_COST = 35;
    constexpr int MANGLE_COST = 34;
    constexpr int RAVAGE_COST = 60;
    constexpr int SHRED_COST = 60;
    constexpr int SWIPE_COST = 45;
    constexpr int CLAW_COST = 40;

    int enemy_count = CountNPCTargetingPlayer(TargetOfTarget(targetGO));
    bool aoe_mode = (enemy_count >= 3);

    
    
    

    
    if (is_ascension && !is_silenced && player_has_spell(faerie_fire_feral_spell))
    {
        if (!IsOnCooldown(faerie_fire_feral_spell) && energy <= 80)
        {
            hardcast(faerie_fire_feral_spell);
            
        }
    }

    
    if (!is_ascension && player_has_spell(faerie_fire_feral_spell))
    {
        bool ff_active = (findAuraTimeLeft(targetGO, faerie_fire_spell, false) > 0) || (
                             findAuraTimeLeft(targetGO, faerie_fire_feral_spell, false) > 0);
        if (!ff_active)
        {
            if (!IsOnCooldown(faerie_fire_feral_spell))
            {
                hardcast(faerie_fire_feral_spell);
                return;
            }
        }
    }

    
    if (player_has_spell(tigers_fury_spell) && !IsOnCooldown(tigers_fury_spell) && !has_berserk)
    {
        if (energy < 35)
        {
            hardcast(tigers_fury_spell);
            return;
        }
    }

    
    if (player_has_spell(berserk_spell) && !IsOnCooldown(berserk_spell))
    {
        if (has_tigers_fury || energy > 60)
        {
            hardcast(berserk_spell);
            return;
        }
    }

    
    if (player_has_spell(kotj_spell) && !IsOnCooldown(kotj_spell))
    {
        if (has_tigers_fury || energy > 60)
        {
            logWarn("-> Action: Cast King of the Jungle\n");
            hardcast(kotj_spell);
            return;
        }
    }

    
    
    
    if (aoe_mode)
    {
        
        if (player_has_spell(savage_roar_spell))
        {
            if ((!has_savage_roar || roar_time_left <= 3) && cp >= 1)
            {
                if (energy >= GetCost(SAVAGE_ROAR_COST))
                {
                    hardcast(savage_roar_spell);
                    return;
                }
                if (!has_savage_roar)
                {
                    return;
                }
            }
        }

        
        if (!has_kotj)
        {
            
            if (!has_savage_roar && cp == 0)
            {
                if (player_has_spell(mangle_cat_spell) && energy >= GetCost(MANGLE_COST))
                {
                    hardcast(mangle_cat_spell);
                    return;
                }
            }

            
            if (player_has_spell(swipe_cat_spell) && energy >= GetCost(SWIPE_COST))
            {
                hardcast(swipe_cat_spell);
                return;
            }

            return;
        }
    }

    if (player_has_spell(feral_frenzy_spell) && !IsOnCooldown(feral_frenzy_spell))
    {
        if (energy >= 45 || has_clearcasting)
        {
            hardcast(feral_frenzy_spell);
            return;
        }
    }

    
    
    

    
    if (player_has_spell(savage_roar_spell) && !has_savage_roar && cp >= 1)
    {
        if (energy >= GetCost(SAVAGE_ROAR_COST))
        {
            hardcast(savage_roar_spell);
            return;
        }

        return;
    }

    
    if (cp >= 5)
    {
        
        if (player_has_spell(rip_spell) && !skip_rip)
        {
            
            if (rip_time_left == 0)
            {
                if (energy >= GetCost(RIP_COST))
                {
                    hardcast(rip_spell);
                    return;
                }

                return;
            }

            
            if (rip_time_left <= 2)
            {
                
                if (bite_refreshes_rip && player_has_spell(ferocious_bite_spell))
                {
                    if (energy >= GetCost(BITE_COST))
                    {
                        hardcast(ferocious_bite_spell);
                        return;
                    }

                    return;
                }

                
                if (energy >= GetCost(RIP_COST))
                {
                    hardcast(rip_spell);
                    return;
                }

                return;
            }
        }

        
        if (player_has_spell(savage_roar_spell))
        {
            bool should_refresh = roar_time_left <= 3 || (force_dump && roar_time_left <= 8);
            if (should_refresh && energy >= GetCost(SAVAGE_ROAR_COST))
            {
                hardcast(savage_roar_spell);
                return;
            }
        }

        
        if (player_has_spell(ferocious_bite_spell))
        {
            bool should_bite = skip_rip || 
                               bite_refreshes_rip || 
                               has_berserk || 
                               force_dump || 
                               (rip_time_left > 4); 

            if (should_bite && energy >= GetCost(BITE_COST))
            {
                hardcast(ferocious_bite_spell);
                return;
            }
        }

        return;
    }

    

    
    if (has_clearcasting)
    {
        if (is_behind && player_has_spell(shred_spell))
        {
            hardcast(shred_spell);
            return;
        }
        if (has_ambush_proc && player_has_spell(ravage_spell))
        {
            hardcast(ravage_spell);
            return;
        }
        if (player_has_spell(mangle_cat_spell))
        {
            hardcast(mangle_cat_spell);
            return;
        }
    }

    
    if (player_has_spell(mangle_cat_spell) && !has_mangle_debuff)
    {
        if (energy >= GetCost(MANGLE_COST))
        {
            hardcast(mangle_cat_spell);
            return;
        }
    }

    
    if (player_has_spell(rake_spell) && rake_time_left <= 2)
    {
        if (energy >= GetCost(RAKE_COST))
        {
            hardcast(rake_spell);
            return;
        }

        return;
    }

    

    
    if (has_ambush_proc && player_has_spell(ravage_spell))
    {
        if (energy >= GetCost(RAVAGE_COST))
        {
            hardcast(ravage_spell);
            return;
        }
    }

    
    if (is_behind && player_has_spell(shred_spell))
    {
        int shred_cost = GetCost(SHRED_COST);
        if (energy >= shred_cost)
        {
            hardcast(shred_spell);
            return;
        }
        
        if (energy >= shred_cost - 5)
        {
            return;
        }
    }

    
    if (player_has_spell(mangle_cat_spell))
    {
        if (energy >= GetCost(MANGLE_COST))
        {
            hardcast(mangle_cat_spell);
            return;
        }
    }

    
    if (!player_has_spell(mangle_cat_spell) && !player_has_spell(shred_spell))
    {
        if (energy >= CLAW_COST)
        {
            hardcast(claw_spell);
            return;
        }
    }
}

void DruidBearRotation(GameObject &targetGO)
{
    auto current_target = TargetGuid();
    auto enermies = CountNPCTargetingMe();
    auto epoch = player_has_spell(swipe_spell);

    auto nearby_npcs = GetNPCsNotTargetingMe();

    for (const auto &entry: nearby_npcs)
    {
        if (!IsOnCooldown(growl_spell))
        {
            cast_mouseover(growl_spell, entry.monster.guid);
        }
    }

    if (player_has_spell(faerie_fire_feral_spell))
    {
        if (!findAura(targetGO, faerie_fire_spell) && !findAura(targetGO, faerie_fire_feral_spell) && !IsOnCooldown(
                faerie_fire_feral_spell))
        {
            hardcast(faerie_fire_feral_spell);
            return;
        }
    }

    if (enermies.size() == 1) hardcast(enrage_spell);

    if (player_has_spell(mangle_bear_spell) && !IsOnCooldown(mangle_bear_spell))
    {
        hardcast(mangle_bear_spell);
        return;
    }

    if (CountNPCTargetingPlayers() >= 2)
    {
        if (player_has_spell(swipe_spell)) hardcast(swipe_spell);
    }

    if (player_has_spell(lacerate_spell))
    {
        auto lacerate = findAuraMax(targetGO, lacerate_spell, true);

        if (lacerate.stack_count != 5 || (lacerate.stack_count >= 1 && lacerate.time_left <= 6))
        {
            hardcast(lacerate_spell);
            return;
        }
    }

    if (player_has_spell(maul_spell)) hardcast(maul_spell);
}

void FeralDruidRotations()
{
    GameObject targetGO{};
    
    if (!findGameObjectByGUID(TargetGuid(), targetGO))
    {
        return; 
    }

    if (disengage_because_flags(targetGO))
    {
        return;
    }

    if (findAuraOnOurself(cat_form_aura))
    {
        DruidCatRotation(targetGO);
        return;
    }
    if (findAuraOnOurself(bear_form_aura) || findAuraOnOurself(dire_bear_form_aura))
    {
        DruidBearRotation(targetGO);
        return;
    }
}

#pragma pack(push, 1)
struct TotemInfo32
{
    uint8_t p[8];
    uint32_t activityFlag;
    uint32_t otherFlag;
    uint32_t namePtr;
    uint8_t ignored[12];
};

struct MultiCastInfo32
{
    uint32_t spellCount;
    uint32_t spellListPtr;
    uint32_t unknown[2];
};
#pragma pack(pop)

struct SimpleTotemInfo
{
    bool isActive;
    uint32_t selectedSpellId;
    std::vector<uint32_t> availableSpells;
};

struct AllTotems
{
    std::array<SimpleTotemInfo, 4> slots;
};

AllTotems readTotemState()
{
    static constexpr uint32_t ACTIVE_BASE = 0x00BD0B00, MC_BASE = 0x00BE8EA4, ACTION_BASE = 0x00c1e358;
    static constexpr int ACTION_OFFSET = 132;

    AllTotems totemState = {};
    for (int i = 0; i < 4; ++i)
    {
        
        TotemInfo32 activeData = read_memory<TotemInfo32>(ACTIVE_BASE + (i * sizeof(TotemInfo32)));
        if (activeData.activityFlag != 0 || activeData.otherFlag != 0)
        {
            totemState.slots[i].isActive = true;
        }

        
        MultiCastInfo32 mcData = read_memory<MultiCastInfo32>(MC_BASE + (i * sizeof(MultiCastInfo32)));
        if (mcData.spellCount > 0 && mcData.spellListPtr != 0)
        {
            totemState.slots[i].availableSpells.reserve(mcData.spellCount);
            for (uint32_t j = 0; j < mcData.spellCount; ++j)
            {
                totemState.slots[i].availableSpells.push_back(read_memory<uint32_t>(mcData.spellListPtr + (j * 4)));
            }
        }

        
        totemState.slots[i].selectedSpellId = read_memory<uint32_t>(ACTION_BASE + ((ACTION_OFFSET + i) * 4));
    }
    return totemState;
}

bool CastMissingTotems(const AllTotems &totemData)
{
    if (player_has_spell(CALL_OF_THE_ELEMENTS))
    {
        bool activity = false;
        for (const auto &slot: totemData.slots)
        {
            if (slot.isActive && slot.selectedSpellId != 0)
            {
                activity = true;
                break;
            }
        }

        if (activity == false)
        {
            hardcast(CALL_OF_THE_ELEMENTS);
            return true;
        }
    }

    for (const auto &slot: totemData.slots)
    {
        if (slot.isActive || slot.selectedSpellId == 0) continue;

        auto it = spell_database.find(slot.selectedSpellId);
        if (it == spell_database.end())
        {
            continue;
        }
        const auto &spell_info = it->second;

        if (!player_has_spell(spell_info.name)) continue;
        if (IsOnCooldown(spell_info.name)) continue;

        if (hardcast(spell_info.name, false))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            return true;
        }
    }

    return false;
}

void ascension_elemental_sham()
{
    GameObject targetGO{};

    auto totem_state = readTotemState();
    if (CastMissingTotems(totem_state)) return;

    if (!findGameObjectByGUID(TargetGuid(), targetGO))
    {
        return;
    }

    if (ourself.health_pct <= 60)
    {
        hardcast(lesser_healing_wave_spell);
        return;
    }

    if (disengage_because_flags(targetGO))
    {
        return;
    }

    auto can_shock = CheckSpell(flame_shock_spell) && CheckSpell(earth_shock_spell) && CheckSpell(frost_shock_spell);

    
    
    auto boomer = findAuraMax(ourself, booming_thunder);
    if (can_shock)
        if (boomer.good)
        {
            if (boomer.stack_count >= 8 && !findAura(targetGO, flame_shock_spell))
            {
                hardcast(earth_shock_spell);
                return;
            }

            if (boomer.stack_count == 15)
            {
                hardcast(earth_shock_spell);
                return;
            }
        }

    if ((!boomer.good || boomer.stack_count <= 1) && can_shock && !findAura(
            targetGO, flame_shock_spell))
    {
        hardcast(flame_shock_spell);
        return;
    }


    if (!findAura(targetGO, water_nova, true) && can_shock)
    {
        hardcast(frost_shock_spell);
        return;
    }

    
    if (CheckSpell(thunderstorm_spell) && manapower_percent() <= 88)
    {
        hardcast(thunderstorm_spell);
        return;
    }

    if (CheckSpell(lavaburst_spell) && findAura(targetGO, flame_shock_spell, true))
    {
        hardcast(lavaburst_spell);
        return;
    }

    
    {
        
        
    }

    bool aoe_mode = false;

    if (CountHostileNPCNearGO(targetGO) > 1) aoe_mode = true;

    if (aoe_mode)
    {
        if (CheckSpell(fire_nova_spell))
        {
            hardcast(fire_nova_spell);
            return;
        }
        if (CheckSpell(chain_lightning_spell))
        {
            hardcast(chain_lightning_spell);
            return;
        }
    }

    hardcast(lightning_bolt_spell);
    return;

    if (player_has_spell(lava_lash_spell))
        if (!IsOnCooldown(lava_lash_spell))
        {
            hardcast(lava_lash_spell);
            return;
        }

    if (player_has_spell(lava_sweep_spell))
        if (!IsOnCooldown(lava_sweep_spell))
        {
            hardcast(lava_sweep_spell);
            return;
        }

    if (player_has_spell(stormstrike_spell))
        if (!IsOnCooldown(stormstrike_spell))
        {
            hardcast(stormstrike_spell);
            return;
        }
}

DeathKnightRunes ReadDeathKnightRunes()
{
    DeathKnightRunes ReturnDKRunes{0, 0, 0, 0};
    
    
    
    
    
    
    uint rune_state = read_memory<uint>(RuneState); 

    for (unsigned int i = 0; i < 6; ++i)
    {
        bool rune_ready = (((1 << i) & rune_state) != 0); 
        if (!rune_ready)
        {
            continue;
        }
        
        uint runeType = read_memory<uint>(RuneType + 4 * i);
        
        
        
        
        
        switch (runeType)
        {
            case 0:
                ReturnDKRunes.blood += 1;
                break;
            case 1:
                ReturnDKRunes.unholy += 1;
                break;
            case 2:
                ReturnDKRunes.frost += 1;
                break;
            case 3:
                ReturnDKRunes.death += 1;
                break;
            default:
                continue;
        }
    }

    return ReturnDKRunes;
}

void deathknight_pet_check()
{
    auto read = read_memory<ulong>(PetGUID);

    GameObject petGO{};
    if (!findGameObjectByGUID(read, petGO))
    {
        logInfo("Pet was not out");
        hardcast(raise_dead_spell);
        return; 
    }

    auto pet_health = read_memory<int>(petGO.descriptor + 0x18 * 4);
    auto pet_max_hp = read_memory<int>(petGO.descriptor + 0x20 * 4);
    auto pet_hp_percent = percent(pet_health, pet_max_hp);

    logInfo("Pet health {}", pet_hp_percent);

    if (pet_hp_percent <= 90)
    {
    }

    if (!findAura(petGO, ghoul_frenzy_spell))
    {
        hardcast(ghoul_frenzy_spell);
    }
}

void DeathKnightUnholyRotation()
{
    GameObject targetGO{};
    if (!IsOnCooldown(horn_of_winter_spell))
    {
        hardcast(horn_of_winter_spell);
        return;
    }

    if (!findAuraOnOurself(bone_shield_spell))
    {
        hardcast(bone_shield_spell);
        return;
    }

    if (!findAuraOnOurself(blood_presence_aura))
    {
        hardcast(blood_presence_aura);
        return;
    }

    if (!findGameObjectByGUID(TargetGuid(), targetGO))
    {
        return; 
    }

    deathknight_pet_check();
    bool aoe_mode = false;

    auto awareness = CountNPCTargetingPlayer(TargetOfTarget(targetGO));

    if (awareness >= 3)
    {
        aoe_mode = true;
    }

    DeathKnightRunes runes = ReadDeathKnightRunes();
    int bloodplague_time = findAuraTimeLeft(targetGO, blood_plague_aura, true);
    int frostfever_time = findAuraTimeLeft(targetGO, frost_fever_aura, true);
    int runic_power = runicpower();

    if (runes.blood == 0 && runes.death == 0)
    {
        hardcast(blood_tap_spell);
        return;
    }

    if (runes.frost == 0 && runes.unholy == 0 && runes.death == 0 && !IsOnCooldown(empower_rune_weapon_spell))
    {
        hardcast(empower_rune_weapon_spell);
        return;
    }

    if (runes.blood >= 1 && !findAuraOnOurself(desolation_aura))
    {
        hardcast(blood_strike_spell);
        return;
    }

    if (runes.unholy >= 1)
    {
        if (bloodplague_time <= 1)
        {
            hardcast(plague_strike_spell);
            return;
        }
    }

    if (runes.frost >= 1)
    {
        if (frostfever_time <= 1)
        {
            hardcast(icy_touch_spell);
            return;
        }
    }

    if (runic_power >= 100)
    {
        hardcast(death_coil_spell);
        return;
    }

    if (IsOnCooldown(summon_gargoyle_spell) && runic_power >= 40)
    {
        hardcast(death_coil_spell);
        return;
    }

    if (runic_power >= 60 && !IsOnCooldown(summon_gargoyle_spell))
    {
        hardcast(summon_gargoyle_spell);
        return;
    }

    if (aoe_mode)
    {
        
        if (runes.blood >= 1 || runes.death >= 1)
        {
            auto total = awareness;
            auto found = 0;
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
        }
    }

    if (!IsOnCooldown(scourge_strike_spell))
    {
        hardcast(scourge_strike_spell);
        return;
    }
}

void DeathKnightFrostRotationDPS()
{
    GameObject targetGO{};
    if (findAuraTimeLeft(ourself, horn_of_winter_spell, false) <= 10)
    {
        hardcast(horn_of_winter_spell);
    }

    if (!findGameObjectByGUID(TargetGuid(), targetGO))
    {
        return; 
    }

    hardcast(horn_of_winter_spell);

    DeathKnightRunes runes = ReadDeathKnightRunes();
    int bloodplague_time = findAuraTimeLeft(targetGO, blood_plague_aura);
    bool bloodplague_ontarget = bloodplague_time > 0;
    int frostfever_time = findAuraTimeLeft(targetGO, frost_fever_aura);
    bool frostfever_ontarget = frostfever_time > 0;
    int runic_power = runicpower();

    
    if (runes.frost >= 1 || runes.death >= 1)
    {
        if (!frostfever_ontarget)
        {
            hardcast(icy_touch_spell);
        }
    }

    if (frostfever_ontarget)
    {
        if (runic_power > 32)
        {
            hardcast(frost_strike_spell);
        }

        if (findAuraOnOurself(freezing_fog_aura))
        {
            hardcast(death_chill_spell);
            hardcast(howling_blast_spell);
        }

        if (runes.blood > 0)
        {
            hardcast(blood_strike_spell);
        }
    }

    if (runes.frost == 0 && runes.unholy == 0 && runes.death == 0)
    {
        hardcast(empower_rune_weapon_spell);
        runes = ReadDeathKnightRunes();
    }

    if (frostfever_time <= 2 || bloodplague_time <= 2)
    {
        if (bloodplague_ontarget && frostfever_ontarget)
        {
            if (runes.blood == 0 && runes.death == 0)
            {
                hardcast(blood_tap_spell);
            }

            hardcast(pestilence_spell);
        }
    }

    if (runes.unholy >= 1 || runes.death >= 1)
    {
        if (!bloodplague_ontarget)
        {
            hardcast(plague_strike_spell);
        }
    }

    if (runes.frost >= 1 || runes.death >= 1)
    {
        hardcast(unbreakable_armor_spell);
    }

    if (runes.blood == 0)
    {
        hardcast(blood_tap_spell);
    }

    if (runes.unholy >= 1 && runes.frost >= 1 || runes.death >= 2)
    {
        if (bloodplague_ontarget && frostfever_ontarget)
        {
            hardcast(obliterate_spell);

            hardcast(blood_strike_spell);

            hardcast(howling_blast_spell);
        }
    }

    if (ourself.health_pct <= 60)
    {
        hardcast(death_strike_spell);
    }
}

TimerBool enough_pyroblast;
TimerBool enough_scorch;

void OnyxiaFireMageRot()
{
    GameObject current_targetGO{};

    if (!findGameObjectByGUID(TargetGuid(), current_targetGO))
    {
        return; 
    }

    if (player_has_spell(scorch_spell))
    {
        auto scorch_info = findAuraTimeLeft(current_targetGO, improved_scorch_aura, false);
        if (scorch_info <= 2)
        {
            if (!enough_scorch)
            {
                hardcast(scorch_spell);
                enough_scorch.begin();
                return;
            }
        }
    }

    if (player_has_spell(pyroblast_spell))
    {
        if (findAuraOnOurself(hot_streak_aura))
        {
            hardcast(pyroblast_spell);
            enough_pyroblast.begin();
            return;
        }
    }

    hardcast(fireball_spell);
}

std::chrono::time_point time_since_last_autoloot = std::chrono::steady_clock::now();
boost::circular_buffer<ulong> units_looted(100);

bool autoloot()
{
    
    return false;
    if (InCombat())
    {
        return false;
    }

    if (is_player_looting())
    {
        return true;
    }

    if (std::chrono::duration_cast<std::chrono::seconds>(now - time_since_last_autoloot).count() < 2)
    {
        return true;
    }
    time_since_last_autoloot = std::chrono::steady_clock::now();

    auto nearby = findNearbyUnits();
    for (const auto &entry: nearby)
    {
        bool skip = false;
        for (const auto &check: units_looted)
        {
            if (check == entry.guid)
            {
                skip = true;
                break;
            }
        }
        if (skip)
        {
            continue;
        }

        if (entry.health_pct != 0)
        {
            continue;
        }
        auto dynflags = ReadDynFlagsForGO(entry);
        if (dynflags & Lootable && dynflags & TaggedByMe)
        {
            units_looted.push_back(entry.guid);
            SetTargetGuid(entry.guid);
            send_key("]");
            return true;
        }
    }

    return false;
}

void set_actionbar()
{
    selected_castbar_addr = FirstActionBarSpellId;

    switch (resolved_player_class)
    {
        case Warrior:
            if (findAuraOnOurself(berserker_stance))
            {
                selected_castbar_addr = FirstActionBarBearForm_and_WarriorBeserkerStance;
            }
            else if (findAuraOnOurself(defensive_stance))
            {
                selected_castbar_addr = FirstActionBarRestoTreeForm_and_WarriorDefensiveStance;
            }
            else if (findAuraOnOurself(battle_stance))
            {
                selected_castbar_addr = FirstActionBarCatForm_ShadowForm_and_WarriorBattleStance;
            }
            return;
        case Hunter:
            return;
        case Rogue:
            if (findAuraOnOurself(stealth_aura))
            {
                selected_castbar_addr = FirstActionBarRogueStealth;
            }
            return;
        case Priest:
            if (findAuraOnOurself(shadowform_aura))
            {
                selected_castbar_addr = FirstActionBarCatForm_ShadowForm_and_WarriorBattleStance;
            }
            return;
        case DeathKnight:
            return;
        case Shaman:
            return;
        case Mage:
            return;
        case Warlock:
            return;
        case Druid:
            if (findAuraOnOurself(tree_of_life_form))
            {
                selected_castbar_addr = FirstActionBarRestoTreeForm_and_WarriorDefensiveStance;
                
            }
            else if (findAuraOnOurself(bear_form_aura) || findAuraOnOurself(dire_bear_form_aura))
            {
                selected_castbar_addr = FirstActionBarBearForm_and_WarriorBeserkerStance;
            }
            else if (findAuraOnOurself(cat_form_aura))
            {
                if (findAuraOnOurself(prowl))
                {
                    selected_castbar_addr = FirstActionBarDruidStealth;
                }
                else
                {
                    selected_castbar_addr = FirstActionBarCatForm_ShadowForm_and_WarriorBattleStance;
                }
            }
            return;
        case Paladin:
            return;
        case Hero:
            return;
    }
}

std::chrono::time_point anti_afk_timer = std::chrono::steady_clock::now();



void AntiAfkAutoDisable()
{
    if (!anti_afk_enabled)
    {
        return;
    }

    auto duration = std::chrono::duration_cast<std::chrono::minutes>(now - anti_afk_timer).count();
    if (duration >= 110)
    {
        anti_afk_enabled = false;
        logWarn("Automatic anti-afk was disabled after {} minutes.", duration);
    }
}

void AntiAFK()
{
    
    
    
    int new_timestamp = wow_timestamp - 1;
    if (!new_timestamp)
    {
        return;
    }
    
    write_memory(LastHardwareAction, new_timestamp);
}

bool enable_toggle = false; 

void rotation_signaled_main()
{
    if (wow_pid == 0)
    {
        return;
    }

    if (ourself.address == 0)
    {
        return;
    }

    
    AntiAFK();
    cacheCooldownInformation();
    
    request_aura_cache_for(ourself);
    gameobjects_seen.push_back(ourself);

    set_actionbar();

    auto current_setting = settings_for_guid(resolved_player_guid);

    switch (current_setting)
    {
        case fishing_mode:
            if (ourself.position.Distance(last_player_position) > 0.0)
            {
                XkbLockModifiers(dpy, XkbUseCoreKbd, 0xff, 0); 
                logInfo("Disabling because of movement");
            }
            else
            {
                fishing_poll();
            }
            break;
        case onyxia_firemage_rot:
            OnyxiaFireMageRot();
            break;
        case paladin_tank_rotation:
            PaladinTankRotation();
            break;
        case paladin_ret_rotation:
            PaladinRetRotation();
            break;
        case healer_mode:
            HealerRotation();
            break;
        case hunter_rotation:
            HunterRotation();
            break;
        case random_rotation:
            RandomRotation();
            break;
        case balance_druid_rotation:
            BalanceDruidRotation();
            break;
        case warrior_arms_rotation:
            ArmsRotation();
            break;
        case warrior_fury_rotation:
            FuryRotation();
            break;
        case shaman_dps_rotation:
            ShamanDpsRotation();
            break;
        case mage_dps_rotation:
            MageDpsRotation();
            break;
        case warlock_dps_rotation:
            WarlockDpsRotation();
            break;
        case feral_mode:
            FeralDruidRotations();
            break;
        case deathknight_rotation:
            DeathKnightFrostRotationDPS();
            break;
        case deathknight_unholy_rotation:
            DeathKnightUnholyRotation();
            break;
        case rogue_sub_mode:
            RogueSubRotation();
            break;
        case rogue_com_mode:
            RogueCombatRotation();
            break;
        case rogue_ass_mode:
            RogueAssasinationRotation();
            break;
        case shadowpriest_mode:
            ShadowPriestRotation();
            break;
        case warrior_tank:
            warrior_tank_rotation();
            break;
        case warrior_armstank:
            warrior_armstank_rotation();
            break;
        case ascension_elemental:
            ascension_elemental_sham();
            break;
        case asc_hunter_rotation:
            ascension_hunter_rotation();
            break;
        case paladin_ret_rotation_ascension:
            PaladinRetRotationAscension();
            break;
        case paladin_tank_rotation_ascension:
            PaladinTankRotationAscension();
            break;
        case priest_bronzebeard_and_epoch_dps:
            PriestFunserverDps();
            break;
        default:
            logWarn("SETTING {} UNKNOWN", static_cast<int>(settings_for_guid(resolved_player_guid)));
            return;
    }
}

void click_mouse()
{
    xdo_mouse_down(x, wow_window, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    xdo_mouse_up(x, wow_window, 1);
}

int login_retry = 0;

void login_try()
{
    return;
    

    if (wow_pid == 0 || wow_window == 0)
    {
        logWarn("Automatic login error, no wow");
        return;
    }

    if (is_player_logged_in())
    {
        logWarn("Automatic login, past login screen");
        return;
    }

    if (is_player_in_world())
    {
        logWarn("Automatic login, in world");
        return;
    }

    ImGui::SetWindowCollapsed(true);

    xdo_activate_window(x, wow_window);

    xdo_click_window(x, wow_window, 1);
    
    xdo_set_window_size(x, wow_window, XWidthOfScreen(screen), XHeightOfScreen(screen), 0);

    
    
    

    trim(AccountSettings.username);
    trim(AccountSettings.password);
    if (AccountSettings.username.empty() || AccountSettings.password.empty())
    {
        logWarn("Automatic login 3");
        return;
    }

    xdo_move_mouse(x, 870, 579, scr);
    click_mouse();
    send_key("CTRL+a");
    std::this_thread::sleep_for(std::chrono::seconds(10));

    if (wow_pid == 0 || wow_window == 0)
    {
        logWarn("Automatic login error, no wow");
        return;
    }

    
    
    if (is_player_logged_in()) return;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    xdo_enter_text_window(x, wow_window, AccountSettings.username.c_str(), 15000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    xdo_move_mouse(x, 880, 670, scr);
    click_mouse();
    send_key("CTRL+a");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    xdo_enter_text_window(x, wow_window, AccountSettings.password.c_str(), 15000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (is_player_logged_in()) return;

    xdo_move_mouse(x, 853, 856, scr);
    click_mouse();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (is_player_logged_in()) return;

    
    xdo_move_mouse(x, 906, 806, scr);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    click_mouse();

    if (is_player_logged_in()) return;

    std::this_thread::sleep_for(std::chrono::seconds(2));
    ++login_retry;

    if (is_player_logged_in()) return;
}

bool process_exists_by_pid(const int &pid)
{
    if (pid <= 0)
    {
        return false;
    }

    struct stat sts{};
    if (stat(fmt::format("/proc/{}", pid).c_str(), &sts) == -1 && errno == ENOENT)
    {
        return false;
    }

    return true;
}

void Debugging_Skip()
{
    logInfo("Name: \"{}\"", get_playername());
    logWarn("From name cache {}", GetPlayerNameFromGuid(resolved_player_guid));

    while (true)
    {
        memory_setup();

        

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

#include "boost/process.hpp"

bool is_ran_as_root_user = (getuid() == 0 && geteuid() == 0);

int X11ErrorHandler([[maybe_unused]] Display *display, [[maybe_unused]] XErrorEvent *error)
{
    return 0; 
}


float RotationToRadians(float rot, float offsetRadians = 0.0f)
{
    const float PI = 3.14159265358979323846f;
    
    return (rot / 6.0f) * 2.0f * PI - PI / 2.0f + offsetRadians;
}


void DrawArrow(ImDrawList *dl, ImVec2 start, ImVec2 end, ImU32 color, float thickness = 1.5f, float headSize = 5.0f)
{
    dl->AddLine(start, end, color, thickness);

    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;

    dx /= len;
    dy /= len;

    float px = -dy;
    float py = dx;

    ImVec2 left(end.x - dx * headSize + px * headSize, end.y - dy * headSize + py * headSize);
    ImVec2 right(end.x - dx * headSize - px * headSize, end.y - dy * headSize - py * headSize);

    dl->AddLine(end, left, color, thickness);
    dl->AddLine(end, right, color, thickness);
}

void RenderRadar()
{
    if (!(ourself.address != 0 && is_player_in_world() && is_player_logged_in())) return;

    ImVec2 screen_size = ImGui::GetIO().DisplaySize;
    float radarSize = 260.0f; 
    float radarPadding = 20.0f;
    float radarRadius = 120.0f; 
    float viewDistance = 620.0f; 
    float scale = radarRadius / viewDistance;

    
    ImVec2 radarPos(screen_size.x - radarSize - radarPadding, radarPadding);

    
    static char searchBuf[64] = "";
    ImGui::SetNextWindowPos(radarPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(radarSize, radarSize + 50.0f), ImGuiCond_Always);
    ImGui::Begin("Radar", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar); 

    ImGui::SetCursorPos(ImVec2(5, 5));
    ImGui::PushItemWidth(radarSize - 10.0f);
    ImGui::InputTextWithHint("##search", "Search...", searchBuf, IM_ARRAYSIZE(searchBuf));
    ImGui::PopItemWidth();

    ImVec2 radarCenter(ImGui::GetWindowPos().x + radarSize * 0.5f, ImGui::GetWindowPos().y + 50.0f + radarSize * 0.5f);

    auto *dl = ImGui::GetWindowDrawList();

    
    ImVec2 playerPos = radarCenter;
    dl->AddCircleFilled(playerPos, 4.0f, IM_COL32(0, 255, 0, 255));

    float playerRot = get_player_rotation();
    float radarRotationOffset = 3.14159265358979323846f / 2.0f; 
    float adjPlayerRot = playerRot + radarRotationOffset;

    ImVec2 playerEnd(playerPos.x + cosf(adjPlayerRot) * 20.0f, playerPos.y - sinf(adjPlayerRot) * 20.0f);
    DrawArrow(dl, playerPos, playerEnd, IM_COL32(0, 255, 0, 255), 2.0f, 7.0f);

    
    int i = 0;
    std::string searchText = searchBuf;
    for (auto &GO: gameobjects_seen)
    {
        if (GO.type == Unit || GO.type == Player) continue;
        if (GO.guid == ourself.guid) continue;

        std::string name = getGameObjectorPlayerName(GO);
        if (name.empty()) continue;

        
        if (!searchText.empty())
        {
            if (name.find(searchText) == std::string::npos) continue;
        }

        float rawRot = getUnitRotation(GO.address);
        bool validRot = !(std::isnan(rawRot) || rawRot < -10.0f || rawRot > 10.0f);

        float dist = GO.position.Distance(ourself.position);

        if (dist > viewDistance) continue;
        if (dist < 0.001) continue;

        float dx = GO.position.x - ourself.position.x;
        float dy = GO.position.y - ourself.position.y;

        
        float angle = radarRotationOffset;
        float rdx = dx * cosf(angle) - dy * sinf(angle);
        float rdy = dx * sinf(angle) + dy * cosf(angle);

        float rx = rdx * scale;
        float ry = -rdy * scale;

        ImVec2 pointPos(radarCenter.x + rx, radarCenter.y + ry);
        dl->AddCircleFilled(pointPos, 3.5f, IM_COL32(255, 0, 0, 255));

        if (validRot)
        {
            float objRot = RotationToRadians(rawRot) + radarRotationOffset;
            float arrowLength = 12.0f * (0.5f + 0.5f * (dist / viewDistance));
            float headSize = 4.0f * (0.5f + 0.5f * (dist / viewDistance));

            ImVec2 arrowEnd(pointPos.x + cosf(objRot) * arrowLength, pointPos.y - sinf(objRot) * arrowLength);
            DrawArrow(dl, pointPos, arrowEnd, IM_COL32(255, 255, 0, 255), 1.5f, headSize);
        }

        
        float offsetX = ((i % 4) - 1.5f) * 26.0f;
        float offsetY = ((i % 3) - 1.0f) * 20.0f;
        ImVec2 textPos(pointPos.x + offsetX, pointPos.y + offsetY);

        dl->AddText(ImVec2(textPos.x + 1, textPos.y + 1), IM_COL32(0, 0, 0, 200), name.c_str()); 
        dl->AddText(textPos, IM_COL32(255, 255, 255, 255), name.c_str()); 

        i++;
    }

    ImGui::End();
}

void do_thing()
{

}

namespace fs = std::filesystem;


bool check_process_status(bool &foundGame, bool &foundLutris)
{
    foundGame = false;
    foundLutris = false;

    try
    {
        for (const auto &entry: fs::directory_iterator("/proc"))
        {
            if (!entry.is_directory()) continue;

            std::string pid = entry.path().filename().string();

            if (!std::ranges::all_of(pid, ::isdigit)) continue;

            std::ifstream cmdFile(entry.path() / "cmdline");

            if (cmdFile.is_open())
            {
                std::string cmdLineContent;
                char c;

                while (cmdFile.get(c))
                {
                    if (c == '\0')
                    {
                        cmdLineContent += ' ';
                    }
                    else
                    {
                        cmdLineContent += c;
                    }
                }
                cmdFile.close();

                if (cmdLineContent.find("wow.exe") != std::string::npos)
                {
                    foundGame = true;
                }

                if (cmdLineContent.find("Wow.exe") != std::string::npos)
                {
                    foundGame = true;
                }

                if (cmdLineContent.find("WoW.exe") != std::string::npos)
                {
                    foundGame = true;
                }

                if (cmdLineContent.find("Ascension.exe") != std::string::npos)
                {
                    foundGame = true;
                }

                if (cmdLineContent.find("lutris") != std::string::npos)
                {
                    foundLutris = true;
                }

                if (foundGame && foundLutris)
                {
                    return true;
                }
            }
        }
    }
    catch (const fs::filesystem_error &e)
    {
        std::cerr << "Filesystem error: " << e.what() << std::endl;
    }

    return false;
}

void DisableSoundGlobally(const uint32_t soundID)
{
    if (soundID == 0 || soundID >= read_memory<uint32_t>(0x00b4ad40)) return;
    uint32_t lookupArrayBase = read_memory<uint32_t>(0x00b4ad44);
    if (lookupArrayBase == 0) return;
    if (read_memory<uint32_t>(lookupArrayBase + soundID * 4) != 0)
    {
        write_memory<uint32_t>(lookupArrayBase + soundID * 4, 0);
    }
}



void MuteAnnoyingSounds()
{
    uint32_t bucketsBase = read_memory<uint32_t>(0x00b4ad4c + 0x1C);
    uint32_t mask = read_memory<uint32_t>(0x00b4ad4c + 0x24);

    if (bucketsBase == 0 || mask == 0xFFFFFFFF)
    {
        return;
    }

    for (uint32_t i = 0; i <= mask; i++)
    {
        uint32_t bucketEntryAddr = bucketsBase + (i * 0x0C);
        int32_t nextPtrOffsetModifier = read_memory<int32_t>(bucketEntryAddr);
        uint32_t currentNodeAddr = read_memory<uint32_t>(bucketEntryAddr + 8);

        while (currentNodeAddr != 0 && (currentNodeAddr & 1) == 0)
        {
            uint32_t namePtr = read_memory<uint32_t>(currentNodeAddr + 0x14);

            if (namePtr != 0)
            {
                std::string soundName = read_memory(namePtr, 128); 

                if (!soundName.empty())
                {
                    bool shouldMute = StringContainsIgnoreCase(soundName, "fizzle") ||
                                      StringContainsIgnoreCase(soundName, "chatscrollbutton") ||
                                      StringContainsIgnoreCase(soundName, "escapescreenclose") ||
                                      StringContainsIgnoreCase(soundName, "uiinterfacebutton") ||
                                      StringEqualsIgnoreCase(soundName, "error"); 

                    if (shouldMute)
                    {
                        uint32_t currentID = read_memory<uint32_t>(currentNodeAddr + 0x18);

                        if (currentID != 0)
                        {
                            DisableSoundGlobally(currentID);
                        }
                    }
                }
            }
            currentNodeAddr = read_memory<uint32_t>(currentNodeAddr + nextPtrOffsetModifier + 4);
        }
    }
}




std::string escapeForDollarQuote(const std::string &rawScript)
{
    std::string result;
    for (char c: rawScript)
    {
        switch (c)
        {
            case '\\': result += "\\\\";
                break; 
            case '\'': result += "\\'";  break;  
            case '\n': result += "\\n";
                break; 
            case '\r': result += "\\r";
                break; 
            case '\t': result += "\\t";
                break; 
            default: result += c;
                break;
        }
    }
    return result;
}

void checkPython()
{
    std::string output;
    char buffer[128];

    
    FILE *pipe = popen("python --version 2>&1", "r");
    if (!pipe)
    {
        fmt::println("Warning: Failed to run python version check.");
        return;
    }

    while (fgets(buffer, sizeof(buffer), pipe) != NULL)
    {
        output += buffer;
    }
    pclose(pipe);

    
    output.erase(std::remove(output.begin(), output.end(), '\n'), output.end());

    std::string version = output;
    size_t last_space = output.find_last_of(" ");
    if (last_space != std::string::npos)
    {
        version = output.substr(last_space + 1);
    }

    fmt::println("Python version: {}", version);

    
    if (version != "3.14.2")
    {
        fmt::println("Attempting upgrade via downgrade...");

        
        std::string rawScript = R"(
echo "-------------------------------------"
echo "Lutris is unlikely to launch without python 3.14.2"
echo ""
echo "Running: yes | sudo downgrade python --latest"
yes | sudo downgrade python --latest
echo ""
echo "-------------------------------------"
exit
exec bash
)";

        
        std::string escaped = escapeForDollarQuote(rawScript);

        
        std::string command = fmt::format("konsole --nofork -e /bin/bash -c $'{}'", escaped);

        
        

        int result = std::system(command.c_str());

        fmt::println("Konsole closed. Resuming application... (Exit Code: {})", result);

        FILE *pipe = popen("python --version 2>&1", "r");
        if (!pipe)
        {
            fmt::println("Warning: Failed to run python version check.");
            return;
        }

        while (fgets(buffer, sizeof(buffer), pipe) != NULL)
        {
            output += buffer;
        }
        pclose(pipe);

        
        output.erase(std::remove(output.begin(), output.end(), '\n'), output.end());

        std::string version = output;
        size_t last_space = output.find_last_of(" ");
        if (last_space != std::string::npos)
        {
            version = output.substr(last_space + 1);
        }

        fmt::println("Python version: {}", version);

        if (version != "3.14.2")
        {
            fmt::println("Python downgrade failed");
        }
    }
    else
    {
        fmt::println("Current python version is {}. No action taken.", version);
    }
}

int main(int, char **)
{
    logAll("Compiler info {} in {} TCMALLOC: {}", CXX_COMPILER_ID, BUILD_TYPE_RAW, HAS_TCMALLOC);
    logAll("Version 1.0.0 WOTLK 12340 ROOT:{}", is_ran_as_root_user);
    if (!isUserInGroup("input"))
    {
        fmt::println("You must put yourself in the 'input' usergroup in order for this program to work!");
        fmt::println("Run \"sudo gpasswd -a $USER input\"");
        fmt::println("Exiting!");
        return 0;
    }
    logAll("User is in input");
    checkPython();

    XSetErrorHandler(X11ErrorHandler);
    
    constexpr ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 0.00f);

    if (is_ran_as_root_user)
    {
        
        logAll("Running as root user, will be in debugging mode!");
    }

    boost::asio::io_context ctxb;
    boost::process::process lutris_process(ctxb, "/usr/bin/lutris", {});

    if (is_ran_as_root_user)
    {
        lutris_process.request_exit();
        lutris_process.wait();
    }

    prctl(PR_SET_PDEATHSIG, SIGHUP);
    XInitThreads();

    dpy = XOpenDisplay(nullptr);
    x = xdo_new_with_opened_display(dpy, ":0", 1);

    
    int count_screens = ScreenCount(dpy);

    logAll("Total count screens: {}", count_screens);

    bool ascensionRunning = false;
    bool lutrisRunning = false;

    for (int i = 0; i < count_screens; ++i)
    {
        screen = ScreenOfDisplay(dpy, i);
        logAll("Info on Screen {}: {}X{}", i + 1, screen->width, screen->height);
    }
    logInfo("Picking display 0");
    screen = ScreenOfDisplay(dpy, 0);

    root_window = screen->root;
    setlocale(LC_ALL, "");

    
    if (is_ran_as_root_user)
    {
        logAll("Waiting for wow...");
        while (wow_pid == 0)
        {
            if (!is_ran_as_root_user && !lutris_process.running())
            {
                break;
            }
            wow_pid = getPidByName("Ascension.exe");
            if (wow_pid == 0) wow_pid = getPidByName("Wow.exe");
            if (wow_pid != 0)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        logAll("Wow_pid {}", wow_pid);
        target_window();
        memory_setup();
        Debugging_Skip(); 
        return 0;
    }
    

    if (!lutris_process.running())
    {
        logAll("Lutris was closed before opening wow");
        cleanup();
        return 0;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    

    target_window();

    STATUS = WAITING_FOR_LOGIN;

    bool minimize_toggle = false;
    bool showing_debug = false;

    XkbLockModifiers(dpy, XkbUseCoreKbd, 0xff, 0); 
    std::chrono::time_point last_login_poll = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
    {
        
        return 1;
    }

    
#if defined(IMGUI_IMPL_OPENGL_ES2)
    
    const char *glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    
    const char *glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); 
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);           
#else
    
    const char *glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    
    
#endif

    
    glfwWindowHint(GLFW_DECORATED, false);
    glfwWindowHint(GLFW_FLOATING, true);
    glfwWindowHint(GLFW_RESIZABLE, false);
    glfwWindowHint(GLFW_FOCUSED, false);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, false);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, true);

    GLFWwindow *window = glfwCreateWindow(expanded_width, expanded_height, "wotlkbot", nullptr, nullptr);
    if (window == nullptr)
    {
        return 1;
    }

    
    int monitors_size = 0;
    

    
    

    
    
    
    
    

    glfwSetWindowPos(window, 0, 0);

    if (glfwGetWindowAttrib(window, GLFW_TRANSPARENT_FRAMEBUFFER))
    {
        logAll("GLFW Transparent framebuffer working.");
    }
    else
    {
        logAll("GLFW Transparent Framebuffer error.");
    }

    glfwMakeContextCurrent(window);

    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void) io;
    
    

    
    ImGui::StyleColorsDark();
    

    
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    std::chrono::time_point last_frame = std::chrono::steady_clock::now();
    std::chrono::time_point last_rotation_poll = std::chrono::steady_clock::now();
    bool showing_account_editor = false;

    
    std::jthread should_run_code_thread1([](const std::stop_token &stoken)
    {
        std::filesystem::path capslock_dir_for_input{};
        bool found = false;

        while (!stoken.stop_requested())
        {
            if (!found)
            {
                for (const auto &dir_entry: std::filesystem::directory_iterator{"/sys/class/leds/"})
                {
                    if (!dir_entry.is_directory())
                    {
                        continue;
                    }
                    if (dir_entry.path().filename().string().find("capslock") == std::string::npos)
                    {
                        continue;
                    }
                    capslock_dir_for_input = dir_entry.path();
                    capslock_dir_for_input /= "brightness";
                    if (std::filesystem::exists(capslock_dir_for_input))
                    {
                        found = true;
                        logInfo("Found capslock LED for input {}", capslock_dir_for_input.string());
                        break;
                    }
                    else
                    {
                        logInfo("capslock LED for input {}, BRIGHTNESS DOESNT EXIST!", capslock_dir_for_input.string());
                    }
                }
            }

            if (!found)
            {
                unsigned state;
                if (XkbGetIndicatorState(dpy, XkbUseCoreKbd, &state) == Success)
                {
                    should_run_code = ((state & 1) != 0);
                }
                else
                {
                    should_run_code = false;
                }
            }

            if (found)
            {
                char c;
                std::ifstream binfile(capslock_dir_for_input, std::ios::binary);
                binfile >> c;
                binfile.close();
                
                if (c != '0')
                {
                    should_run_code = true;
                }
                else
                {
                    should_run_code = false;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(16)); 
        }
    });

    
    ChiptuneSound chipSound;

    chipSound.playDisable();

    bool first_loop = true;
    
    while (!glfwWindowShouldClose(window))
    {
        now = std::chrono::steady_clock::now();

        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("wotlkbot", nullptr,
                     (ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollWithMouse |
                      ImGuiWindowFlags_NoSavedSettings));

        if (should_run_code)
        {
            ImGui::SetWindowCollapsed(true);
        }

        if (first_loop)
        {
            ImGui::SetWindowCollapsed(true);
            first_loop = false;
        }

        if (wow_pid == 0)
        {
            if (ImGui::Button("Edit Saved Account"))
            {
                showing_account_editor = true;
            }

            if (showing_account_editor)
            {
                ImGui::InputText("Username", &AccountSettings.username, 0);
                ImGui::InputText("Password", &AccountSettings.password, 0);
            }
        }

        if (ImGui::IsWindowCollapsed())
        {
            if (!minimize_toggle)
            {
                minimize_toggle = true;
                glfwSetWindowSize(window, collapsed_width, collapsed_height);
            }
        }
        else
        {
            if (minimize_toggle)
            {
                minimize_toggle = false;
                glfwSetWindowSize(window, expanded_width, expanded_height);
            }
        }
        ImGui::SetWindowPos(ImVec2{0, 0});

        if (STATUS == READY)
        {
            last_player_position = ourself.position; 
        }

        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_login_poll).count() > 5)
        {
            last_login_poll = now;
            bool wow_pid_was_zero = wow_pid == 0;

            if (wow_pid != 0 && !process_exists_by_pid(wow_pid))
            {
                logAll("Wow pid closed");
                STATUS = WAITING_FOR_WOW;

                resetPlayerPtr();

                wow_pid = 0;
                login_retry = 0;
                wow_window = 0;
            }

            if (wow_window == 0 && STATUS != WAITING_FOR_WOW)
            {
                target_window(); 
            }

            if (wow_pid == 0)
            {
                wow_pid = getPidByName("Ascension.exe");
                if (wow_pid == 0) wow_pid = getPidByName("Wow.exe");
                if (wow_pid == 0)
                {
                    STATUS = WAITING_FOR_WOW;
                }
                else
                {
                    login_retry = 0;
                    STATUS = WAITING_FOR_LOGIN;
                }
            }

            if (wow_pid_was_zero && wow_pid != 0)
            {
                STATUS = WAITING_FOR_LOGIN;
                login_retry = 0;
                
                auto version = read_memory<int>(4208249);
                if (version == 0)
                {
                    continue;
                } 
                if (version != 12340)
                {
                    logAll("Game engine version is incompatible");
                    break;
                }
            }
            else
            {
                showing_account_editor = false;
            }

            if (STATUS == READY && !is_player_in_world())
            {
                showing_account_editor = false;
                STATUS = WAITING_FOR_LOGIN;
                
                ImGui::SetWindowCollapsed(true);
            }

            if (STATUS == WAITING_FOR_LOGIN)
            {
                if (memory_setup())
                {
                    STATUS = READY;
                    login_retry = 0;
                }
            }

            if (STATUS == WAITING_FOR_LOGIN && !is_player_logged_in() && login_retry == 0)
            {
                login_try();
            }
        }

        
        
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_rotation_poll).count() > 60)
        {
            last_rotation_poll = now;
            memory_setup(); 
            debug_macros();

            if (is_player_in_world())
            {
                if (should_run_code)
                {
                    if (!enable_toggle)
                    {
                        enable_toggle = true;
                        target_window();
                        chipSound.playEnable();
                        MuteAnnoyingSounds();
                        setup_spellbook_database();
                        last_player_position = ourself.position;
                    }

                    rotation_signaled_main();
                }
                else
                {
                    if (enable_toggle)
                    {
                        enable_toggle = false;
                        chipSound.playDisable();
                        reset_actionbar();
                    }

                    if (should_run_code)
                    {
                        XkbLockModifiers(dpy, XkbUseCoreKbd, 0xff, 0); 
                        tracking_hack_enabled = false;
                        anti_afk_enabled = false;
                    }

                    if (anti_afk_enabled)
                    {
                        AntiAFK();
                        AntiAfkAutoDisable();
                    }
                }
            }
        }

        if (ImGui::Button("Cool thing"))
        {
            
            MuteAnnoyingSounds();
        }

        check_process_status(ascensionRunning, lutrisRunning);
        if (!ascensionRunning && !lutrisRunning)
        {
            
            
            
            
            
            break;
        }
#ifdef DEBUG

        if (ImGui::Button("Exit"))
        {
            kill(wow_pid, SIGHUP);

            if (lutris_process.running())
            {
                lutris_process.terminate();
            }
            break;
        }

        if (ImGui::Button("Debug"))
        {
            showing_debug = !showing_debug;
        }

        if (showing_debug)
        {
            auto mousepos = current_mouse_pos();
            ImGui::TextUnformatted(fmt::format("x:{} y:{}", mousepos.x, mousepos.y).c_str());
        }
#endif

        if (ImGui::BeginMenu("Configuration", true))
        {
            if (ImGui::BeginMenu("Modes", true))
            {
                auto CurrentlyActiveSetting = settings_for_guid(resolved_player_guid);

                bool can_heal = false;
                if (resolved_player_guid != 0)
                    switch (resolved_player_class)
                    {
                        case Warrior:
                            if (ImGui::MenuItem("Warrior Arms", nullptr,
                                                CurrentlyActiveSetting == warrior_arms_rotation))
                            {
                                SetSettingForUID(resolved_player_guid, warrior_arms_rotation);
                            }

                            if (ImGui::MenuItem("Warrior Fury", nullptr,
                                                CurrentlyActiveSetting == warrior_fury_rotation))
                            {
                                SetSettingForUID(resolved_player_guid, warrior_fury_rotation);
                            }
                            if (ImGui::MenuItem("Protwarrior", nullptr, CurrentlyActiveSetting == warrior_tank))
                            {
                                SetSettingForUID(resolved_player_guid, warrior_tank);
                            }

                            if (ImGui::MenuItem("Protwarrior arms", nullptr,
                                                CurrentlyActiveSetting == warrior_armstank))
                            {
                                SetSettingForUID(resolved_player_guid, warrior_armstank);
                            }
                            break;
                        case Paladin:
                            if (ImGui::MenuItem("Paladin (Tank)", nullptr,
                                                CurrentlyActiveSetting == paladin_tank_rotation))
                            {
                                SetSettingForUID(resolved_player_guid, paladin_tank_rotation);
                            }

                            if (ImGui::MenuItem("Paladin (Ret)", nullptr,
                                                CurrentlyActiveSetting == paladin_ret_rotation))
                            {
                                SetSettingForUID(resolved_player_guid, paladin_ret_rotation);
                            }
                            if (ImGui::MenuItem("PaladinRetAscension", nullptr,
                                                CurrentlyActiveSetting == paladin_ret_rotation_ascension))
                            {
                                SetSettingForUID(resolved_player_guid, paladin_ret_rotation_ascension);
                            }
                            if (ImGui::MenuItem("PaladinTankAscension", nullptr,
                                                CurrentlyActiveSetting == paladin_tank_rotation_ascension))
                            {
                                SetSettingForUID(resolved_player_guid, paladin_tank_rotation_ascension);
                            }
                            can_heal = true;
                            break;
                        case Hunter:
                            if (ImGui::MenuItem("Hunter", nullptr, CurrentlyActiveSetting == hunter_rotation))
                            {
                                SetSettingForUID(resolved_player_guid, hunter_rotation);
                            }
                            if (ImGui::MenuItem("Asc Hunter", nullptr, CurrentlyActiveSetting == asc_hunter_rotation))
                            {
                                SetSettingForUID(resolved_player_guid, asc_hunter_rotation);
                            }
                            break;
                        case Rogue:
                            if (ImGui::MenuItem("Rogue Sub", nullptr, CurrentlyActiveSetting == rogue_sub_mode))
                            {
                                SetSettingForUID(resolved_player_guid, rogue_sub_mode);
                            }
                            if (ImGui::MenuItem("Rogue Combat", nullptr, CurrentlyActiveSetting == rogue_com_mode))
                            {
                                SetSettingForUID(resolved_player_guid, rogue_com_mode);
                            }

                            if (ImGui::MenuItem("Rogue Assassin", nullptr, CurrentlyActiveSetting == rogue_ass_mode))
                            {
                                SetSettingForUID(resolved_player_guid, rogue_ass_mode);
                            }
                            break;
                        case Priest:
                            if (ImGui::MenuItem("Shadowpriest", nullptr, CurrentlyActiveSetting == shadowpriest_mode))
                            {
                                SetSettingForUID(resolved_player_guid, shadowpriest_mode);
                            }

                            if (ImGui::MenuItem("Funserver dps", nullptr,
                                                CurrentlyActiveSetting == priest_bronzebeard_and_epoch_dps))
                            {
                                SetSettingForUID(resolved_player_guid, priest_bronzebeard_and_epoch_dps);
                            }
                            can_heal = true;
                            break;
                        case DeathKnight:
                            if (ImGui::MenuItem("Deathknight Frost DPS", nullptr,
                                                CurrentlyActiveSetting == deathknight_rotation))
                            {
                                SetSettingForUID(resolved_player_guid, deathknight_rotation);
                            }

                            if (ImGui::MenuItem("Deathknight Unholy DPS", nullptr,
                                                CurrentlyActiveSetting == deathknight_unholy_rotation))
                            {
                                SetSettingForUID(resolved_player_guid, deathknight_unholy_rotation);
                            }
                            break;
                        case Shaman:
                            if (ImGui::MenuItem("Shaman DPS", nullptr, CurrentlyActiveSetting == shaman_dps_rotation))
                            {
                                SetSettingForUID(resolved_player_guid, shaman_dps_rotation);
                            }
                            if (ImGui::MenuItem("Asc Ele Sham", nullptr, CurrentlyActiveSetting == ascension_elemental))
                            {
                                SetSettingForUID(resolved_player_guid, ascension_elemental);
                            }
                            can_heal = true;
                            break;
                        case Mage:
                            if (ImGui::MenuItem("OnyxiaFireMage", nullptr,
                                                CurrentlyActiveSetting == onyxia_firemage_rot))
                            {
                                SetSettingForUID(resolved_player_guid, onyxia_firemage_rot);
                            }
                            if (ImGui::MenuItem("Mage DPS", nullptr, CurrentlyActiveSetting == mage_dps_rotation))
                            {
                                SetSettingForUID(resolved_player_guid, mage_dps_rotation);
                            }

                            break;
                        case Warlock:
                            if (ImGui::MenuItem("Warlock DPS", nullptr, CurrentlyActiveSetting == warlock_dps_rotation))
                            {
                                SetSettingForUID(resolved_player_guid, warlock_dps_rotation);
                            }
                            break;
                        case Druid:
                            if (ImGui::MenuItem("Balance Druid", nullptr,
                                                CurrentlyActiveSetting == balance_druid_rotation))
                            {
                                SetSettingForUID(resolved_player_guid, balance_druid_rotation);
                            }

                            if (ImGui::MenuItem("Feral Druid", nullptr, CurrentlyActiveSetting == feral_mode))
                            {
                                SetSettingForUID(resolved_player_guid, feral_mode);
                            }
                        case Hero: 
                            {
                                can_heal = true;
                                break;
                            }
                            can_heal = true;
                            break;
                    }

                if (ImGui::MenuItem("Fishing!", nullptr, CurrentlyActiveSetting == fishing_mode))
                {
                    SetSettingForUID(resolved_player_guid, fishing_mode);
                }

                if (ImGui::MenuItem("Random", nullptr, CurrentlyActiveSetting == random_rotation))
                {
                    SetSettingForUID(resolved_player_guid, random_rotation);
                }

                if (can_heal)
                    if (ImGui::MenuItem("Healer", nullptr, CurrentlyActiveSetting == healer_mode))
                    {
                        SetSettingForUID(resolved_player_guid, healer_mode);
                    }

                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        ImGui::Checkbox("alt rot mode", &alternative_rotation_mode);
        ImGui::Checkbox("Auto taunt", &automatic_taunting);
        ImGui::Checkbox("Automatic flag", &automatic_flag);
        ImGui::Checkbox("Tracking", &tracking_hack_enabled);
        if (ImGui::Checkbox("Anti-Afk", &anti_afk_enabled))
        {
            anti_afk_timer = std::chrono::steady_clock::now();
            logWarn("Anti-Afk is now active");
        }

        ImGui::Checkbox("Radar", &showing_radar);
        if (ImGui::Checkbox("AutoInteract", &automatic_interact))
        {
            ImGui::SetWindowCollapsed(true);
            last_player_positionInteract = ourself.position;
        }

        ImGui::TextUnformatted(fmt::sprintf("Map %d", current_map_id).c_str());

        ImGui::Separator();

        if (showing_debug)
        {
            ImGui::TextUnformatted(fmt::sprintf("Application average %.3f ms/frame (%.1f FPS)",
                                                1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate).c_str());
        }
        ImGui::End();

        if (showing_radar)
        {
            RenderRadar();
        }

        
        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w,
                     clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    
    should_run_code_thread1.request_stop();

    ImGui_ImplOpenGL3_Shutdown();

    ImGui_ImplGlfw_Shutdown();

    ImGui::DestroyContext();

    glfwDestroyWindow(window);

    std::atexit(cleanup);
    std::at_quick_exit(cleanup);

    glfwTerminate();

    return 0;
}
