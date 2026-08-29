/*
 * Simulates classic world buff turn-ins by choosing a faction-appropriate
 * character name, announcing the event, and applying the actual world buffs.
 */

#include "AreaDefines.h"
#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldSessionMgr.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace
{
constexpr uint32 SPELL_WARCHIEFS_BLESSING = 16609;
constexpr uint32 SPELL_RALLYING_CRY_OF_THE_DRAGONSLAYER = 22888;
constexpr uint32 SPELL_SPIRIT_OF_ZANDALAR = 24425;
constexpr uint32 MS_PER_MINUTE = 60 * IN_MILLISECONDS;

enum class AnnouncerNamePool
{
    Alliance,
    Horde,
    Either
};

constexpr std::array<char const*, 24> ALLIANCE_ANNOUNCER_NAMES = {{
    "Aldren", "Aricel", "Baelric", "Brennar", "Caldrin", "Corwyn",
    "Darien", "Edrin", "Elowen", "Fenric", "Garran", "Halwen",
    "Isendra", "Jorren", "Kaelyn", "Liora", "Merric", "Norwyn",
    "Odelia", "Perrin", "Roswyn", "Selric", "Tarian", "Vaelora"
}};

constexpr std::array<char const*, 24> HORDE_ANNOUNCER_NAMES = {{
    "Azgora", "Brakka", "Dorgash", "Gorvak", "Gralnok", "Hargun",
    "Krazka", "Lokgar", "Malkor", "Mogra", "Narvok", "Rethka",
    "Rogash", "Shagra", "Surnak", "Thokran", "Torgha", "Urgash",
    "Varkesh", "Varzok", "Zagrim", "Zoraka", "Drezka", "Kazra"
}};

struct DelayedCast
{
    uint32 SpellId;
    uint32 AreaId;
    uint32 TimerMs;
};

struct WorldBuffEvent
{
    char const* Key;
    char const* Label;
    uint32 SpellId;
    AnnouncerNamePool NamePool;
    bool Enabled;
    uint32 TimerMs;
    std::string DefaultAnnouncement;
    std::string Announcement;
    std::vector<uint32> TargetAreaIds;
};

bool IsInAreaOrZone(Player const* player, uint32 areaId)
{
    return player && (player->GetAreaId() == areaId || player->GetZoneId() == areaId);
}

bool IsEligibleTarget(Player const* player)
{
    return player && player->IsInWorld() && player->IsAlive() && !player->IsGameMaster();
}

uint32 MinutesToMs(uint32 minutes)
{
    return minutes * MS_PER_MINUTE;
}

void ReplaceAll(std::string& text, std::string const& token, std::string const& value)
{
    std::size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos)
    {
        text.replace(pos, token.length(), value);
        pos += value.length();
    }
}
}

class WorldBuffBotsWorldScript : public WorldScript
{
public:
    WorldBuffBotsWorldScript() : WorldScript("WorldBuffBotsWorldScript", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_STARTUP,
        WORLDHOOK_ON_UPDATE
    }),
        _events{{
            {
                "Warchief",
                "Warchief's Blessing",
                SPELL_WARCHIEFS_BLESSING,
                AnnouncerNamePool::Horde,
                true,
                0,
                "Rend Blackhand has fallen! Thrall has granted Warchief's Blessing in honor of {player}.",
                {},
                { AREA_ORGRIMMAR }
            },
            {
                "Dragonslayer",
                "Rallying Cry of the Dragonslayer",
                SPELL_RALLYING_CRY_OF_THE_DRAGONSLAYER,
                AnnouncerNamePool::Alliance,
                true,
                0,
                "{player} has returned the head of Onyxia! Rallying Cry of the Dragonslayer echoes through Stormwind.",
                {},
                { AREA_STORMWIND_CITY }
            },
            {
                "Zandalar",
                "Spirit of Zandalar",
                SPELL_SPIRIT_OF_ZANDALAR,
                AnnouncerNamePool::Either,
                true,
                0,
                "{player} has returned the Heart of Hakkar! Spirit of Zandalar fills Stranglethorn Vale.",
                {},
                { AREA_STRANGLETHORN_VALE }
            }
        }}
    {
    }

    void OnAfterConfigLoad(bool reload) override
    {
        LoadConfig();

        if (reload)
        {
            _delayedCasts.clear();
            ResetTimers();
        }
    }

    void OnStartup() override
    {
        ResetTimers();
    }

    void OnUpdate(uint32 diff) override
    {
        if (!_enabled)
            return;

        UpdateDelayedCasts(diff);

        for (WorldBuffEvent& event : _events)
        {
            if (!event.Enabled)
                continue;

            if (event.TimerMs <= diff)
            {
                FireEvent(event);
                event.TimerMs = RollMainDelayMs();
            }
            else
            {
                event.TimerMs -= diff;
            }
        }
    }

private:
    void LoadConfig()
    {
        _enabled = sConfigMgr->GetOption<bool>("WorldBuffBots.Enable", true);
        _debug = sConfigMgr->GetOption<bool>("WorldBuffBots.Debug", false);
        _baseMinutes = sConfigMgr->GetOption<uint32>("WorldBuffBots.BaseMinutes", 90);
        _varianceMinutes = sConfigMgr->GetOption<uint32>("WorldBuffBots.VarianceMinutes", 60);
        _initialMinMinutes = sConfigMgr->GetOption<uint32>("WorldBuffBots.InitialMinMinutes", 30);
        _initialMaxMinutes = sConfigMgr->GetOption<uint32>("WorldBuffBots.InitialMaxMinutes", 150);
        _warchiefCrossroads = sConfigMgr->GetOption<bool>("WorldBuffBots.Warchief.IncludeCrossroads", true);

        for (WorldBuffEvent& event : _events)
        {
            std::string prefix = "WorldBuffBots.";
            prefix += event.Key;

            event.Enabled = sConfigMgr->GetOption<bool>(prefix + ".Enable", true);
            event.Announcement = sConfigMgr->GetOption<std::string>(prefix + ".Announcement", event.DefaultAnnouncement);
        }
    }

    void ResetTimers()
    {
        for (WorldBuffEvent& event : _events)
            event.TimerMs = RollInitialDelayMs();

        if (_debug)
            LOG_INFO("module", "WorldBuffBots: timers reset");
    }

    uint32 RollInitialDelayMs() const
    {
        uint32 minMinutes = std::max<uint32>(1, _initialMinMinutes);
        uint32 maxMinutes = std::max<uint32>(1, _initialMaxMinutes);

        if (maxMinutes < minMinutes)
            std::swap(maxMinutes, minMinutes);

        return MinutesToMs(urand(minMinutes, maxMinutes));
    }

    uint32 RollMainDelayMs() const
    {
        uint32 minMinutes = _baseMinutes > _varianceMinutes ? _baseMinutes - _varianceMinutes : 1;
        uint32 maxMinutes = std::max<uint32>(minMinutes, _baseMinutes + _varianceMinutes);
        return MinutesToMs(urand(minMinutes, maxMinutes));
    }

    void FireEvent(WorldBuffEvent const& event)
    {
        std::string announcerName = SelectAnnouncerName(event.NamePool);
        std::string announcement = FormatAnnouncement(event, announcerName);

        sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, announcement);

        uint32 appliedCount = 0;
        for (uint32 areaId : event.TargetAreaIds)
            appliedCount += ApplySpellToArea(event.SpellId, areaId);

        if (event.SpellId == SPELL_WARCHIEFS_BLESSING && _warchiefCrossroads)
            _delayedCasts.push_back({ event.SpellId, AREA_THE_CROSSROADS, 10 * IN_MILLISECONDS });

        LOG_INFO("module", "WorldBuffBots: fired {} using announcer '{}' and applied {} immediate buffs",
            event.Label, announcerName, appliedCount);
    }

    void UpdateDelayedCasts(uint32 diff)
    {
        for (auto itr = _delayedCasts.begin(); itr != _delayedCasts.end();)
        {
            if (itr->TimerMs <= diff)
            {
                uint32 appliedCount = ApplySpellToArea(itr->SpellId, itr->AreaId);

                if (_debug)
                    LOG_INFO("module", "WorldBuffBots: delayed spell {} applied to {} players in area {}", itr->SpellId, appliedCount, itr->AreaId);

                itr = _delayedCasts.erase(itr);
            }
            else
            {
                itr->TimerMs -= diff;
                ++itr;
            }
        }
    }

    uint32 ApplySpellToArea(uint32 spellId, uint32 areaId) const
    {
        uint32 appliedCount = 0;

        sWorldSessionMgr->DoForAllOnlinePlayers([&](Player* player)
        {
            if (!IsEligibleTarget(player) || !IsInAreaOrZone(player, areaId))
                return;

            player->CastSpell(player, spellId, true);
            ++appliedCount;
        });

        return appliedCount;
    }

    std::string SelectAnnouncerName(AnnouncerNamePool namePool) const
    {
        switch (namePool)
        {
            case AnnouncerNamePool::Alliance:
                return PickRandomName(ALLIANCE_ANNOUNCER_NAMES);
            case AnnouncerNamePool::Horde:
                return PickRandomName(HORDE_ANNOUNCER_NAMES);
            case AnnouncerNamePool::Either:
                return urand(0, 1) == 0
                    ? PickRandomName(ALLIANCE_ANNOUNCER_NAMES)
                    : PickRandomName(HORDE_ANNOUNCER_NAMES);
        }

        return PickRandomName(ALLIANCE_ANNOUNCER_NAMES);
    }

    template <std::size_t N>
    std::string PickRandomName(std::array<char const*, N> const& names) const
    {
        static_assert(N > 0, "Announcer name pools cannot be empty");
        return names[urand(0, static_cast<uint32>(N - 1))];
    }

    std::string FormatAnnouncement(WorldBuffEvent const& event, std::string const& announcerName) const
    {
        std::string message = event.Announcement;
        ReplaceAll(message, "{player}", announcerName);
        ReplaceAll(message, "{buff}", event.Label);
        ReplaceAll(message, "%s", announcerName);
        return message;
    }

    std::array<WorldBuffEvent, 3> _events;
    std::vector<DelayedCast> _delayedCasts;

    bool _enabled = true;
    bool _debug = false;
    bool _warchiefCrossroads = true;
    uint32 _baseMinutes = 90;
    uint32 _varianceMinutes = 60;
    uint32 _initialMinMinutes = 30;
    uint32 _initialMaxMinutes = 150;
};

void AddWorldBuffBotsScripts()
{
    new WorldBuffBotsWorldScript();
}
