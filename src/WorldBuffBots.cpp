/*
 * Simulates classic world buff turn-ins by choosing a faction-appropriate
 * character name, warning that faction ahead of time, announcing the event,
 * and applying the actual world buffs.
 *
 * All buffs share one cycle so the Horde and Alliance city turn-ins land at the
 * same moment. Spirit of Zandalar is offset later within that same cycle, so
 * players who caught a city buff have time to travel to Zul'Gurub for it.
 */

#include "AreaDefines.h"
#include "Config.h"
#include "GameTime.h"
#include "Log.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "World.h"
#include "WorldSessionMgr.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr uint32 SPELL_WARCHIEFS_BLESSING = 16609;
constexpr uint32 SPELL_RALLYING_CRY_OF_THE_DRAGONSLAYER = 22888;
constexpr uint32 SPELL_SPIRIT_OF_ZANDALAR = 24425;
constexpr uint32 MS_PER_MINUTE = 60 * IN_MILLISECONDS;

enum class BuffFaction
{
    Alliance,
    Horde,
    Both
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
    BuffFaction Faction;
    uint32 TimerMs;
};

struct WorldBuffEvent
{
    WorldBuffEvent(char const* key, char const* label, uint32 spellId, BuffFaction faction,
        uint32 defaultOffsetMinutes, char const* defaultAnnouncement, char const* defaultWarning,
        std::vector<uint32> targetAreaIds)
        : Key(key), Label(label), SpellId(spellId), Faction(faction),
          DefaultOffsetMinutes(defaultOffsetMinutes), DefaultAnnouncement(defaultAnnouncement),
          DefaultWarning(defaultWarning), TargetAreaIds(std::move(targetAreaIds))
    {
    }

    // Fixed description of the event, set once at construction.
    char const* Key;
    char const* Label;
    uint32 SpellId;
    BuffFaction Faction;
    uint32 DefaultOffsetMinutes;
    char const* DefaultAnnouncement;
    char const* DefaultWarning;
    std::vector<uint32> TargetAreaIds;

    // Reloaded from the config file.
    bool Enabled = true;
    uint32 OffsetMs = 0;
    std::string Announcement;
    std::string Warning;

    // State for the cycle currently in flight.
    uint32 TimerMs = 0;
    bool Pending = false;
    bool WarningSent = false;
    std::string PendingAnnouncer;
};

bool IsInAreaOrZone(Player const* player, uint32 areaId)
{
    return player && (player->GetAreaId() == areaId || player->GetZoneId() == areaId);
}

bool IsEligibleTarget(Player const* player)
{
    return player && player->IsInWorld() && player->IsAlive() && !player->IsGameMaster();
}

bool MatchesFaction(Player const* player, BuffFaction faction)
{
    if (faction == BuffFaction::Both)
        return true;

    TeamId teamId = faction == BuffFaction::Horde ? TEAM_HORDE : TEAM_ALLIANCE;
    return player && player->GetTeamId() == teamId;
}

uint32 MinutesToMs(uint32 minutes)
{
    return minutes * MS_PER_MINUTE;
}

std::string FormatMinutes(uint32 minutes)
{
    return std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
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
                BuffFaction::Horde,
                0,
                "Rend Blackhand has fallen! Thrall has granted Warchief's Blessing in honor of {player}.",
                "{player} is carrying Rend Blackhand's head to Orgrimmar. Warchief's Blessing drops at {time} realm time!",
                { AREA_ORGRIMMAR }
            },
            {
                "Dragonslayer",
                "Rallying Cry of the Dragonslayer",
                SPELL_RALLYING_CRY_OF_THE_DRAGONSLAYER,
                BuffFaction::Alliance,
                0,
                "{player} has returned the head of Onyxia! Rallying Cry of the Dragonslayer echoes through Stormwind.",
                "{player} is carrying the head of Onyxia to Stormwind. Rallying Cry of the Dragonslayer drops at {time} realm time!",
                { AREA_STORMWIND_CITY }
            },
            {
                "Zandalar",
                "Spirit of Zandalar",
                SPELL_SPIRIT_OF_ZANDALAR,
                BuffFaction::Both,
                10,
                "{player} has returned the Heart of Hakkar! Spirit of Zandalar fills Stranglethorn Vale.",
                "{player} is carrying the Heart of Hakkar to Yojamba Isle. Spirit of Zandalar drops at {time} realm time!",
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
            if (!event.Enabled || !event.Pending)
                continue;

            if (!event.WarningSent && _warningLeadMs && event.TimerMs <= _warningLeadMs)
            {
                SendWarning(event);
                event.WarningSent = true;
            }

            if (event.TimerMs <= diff)
            {
                FireEvent(event);
                event.Pending = false;
            }
            else
            {
                event.TimerMs -= diff;
            }
        }

        // The whole cycle rerolls together, but only once its last offset event
        // has fired, so an in-flight Spirit of Zandalar is never clobbered.
        if (HasEnabledEvent() && !IsCycleInFlight())
            ArmCycle(RollMainDelayMs(), true);
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
        _warningLeadMs = MinutesToMs(sConfigMgr->GetOption<uint32>("WorldBuffBots.WarningMinutes", 10));
        _restrictBuffToFaction = sConfigMgr->GetOption<bool>("WorldBuffBots.RestrictBuffToFaction", true);
        _timeFormat = sConfigMgr->GetOption<std::string>("WorldBuffBots.TimeFormat", "%H:%M");
        _warchiefCrossroads = sConfigMgr->GetOption<bool>("WorldBuffBots.Warchief.IncludeCrossroads", true);

        for (WorldBuffEvent& event : _events)
        {
            std::string prefix = "WorldBuffBots.";
            prefix += event.Key;

            event.Enabled = sConfigMgr->GetOption<bool>(prefix + ".Enable", true);
            event.OffsetMs = MinutesToMs(sConfigMgr->GetOption<uint32>(prefix + ".OffsetMinutes", event.DefaultOffsetMinutes));
            event.Announcement = sConfigMgr->GetOption<std::string>(prefix + ".Announcement", event.DefaultAnnouncement);
            event.Warning = sConfigMgr->GetOption<std::string>(prefix + ".Warning", event.DefaultWarning);
        }
    }

    void ResetTimers()
    {
        ArmCycle(RollInitialDelayMs(), false);

        if (_debug)
            LOG_INFO("module", "WorldBuffBots: timers reset");
    }

    bool HasEnabledEvent() const
    {
        return std::any_of(_events.begin(), _events.end(),
            [](WorldBuffEvent const& event) { return event.Enabled; });
    }

    bool IsCycleInFlight() const
    {
        return std::any_of(_events.begin(), _events.end(),
            [](WorldBuffEvent const& event) { return event.Enabled && event.Pending; });
    }

    // Largest offset among enabled events, i.e. how long a cycle runs from the
    // city turn-in until its last event has fired.
    uint32 CycleTailMs() const
    {
        uint32 tail = 0;
        for (WorldBuffEvent const& event : _events)
            if (event.Enabled)
                tail = std::max(tail, event.OffsetMs);

        return tail;
    }

    // delayMs is measured between consecutive city turn-ins. A reroll happens
    // once the cycle's tail has already elapsed, so subtract it back out to
    // keep that spacing honest.
    void ArmCycle(uint32 delayMs, bool subtractTail)
    {
        uint32 tailMs = subtractTail ? CycleTailMs() : 0;
        uint32 cityDelayMs = delayMs > tailMs ? delayMs - tailMs : 1;

        for (WorldBuffEvent& event : _events)
        {
            event.TimerMs = cityDelayMs + event.OffsetMs;
            event.Pending = true;
            event.WarningSent = false;
            event.PendingAnnouncer = SelectAnnouncerName(event.Faction);

            if (_debug && event.Enabled)
                LOG_INFO("module", "WorldBuffBots: {} armed for {} minutes with announcer '{}'",
                    event.Label, event.TimerMs / MS_PER_MINUTE, event.PendingAnnouncer);
        }
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

    void SendWarning(WorldBuffEvent const& event) const
    {
        // A reroll shorter than the lead time leaves no useful warning window.
        if (event.Warning.empty() || event.TimerMs < MS_PER_MINUTE)
            return;

        uint32 minutes = (event.TimerMs + MS_PER_MINUTE - 1) / MS_PER_MINUTE;
        std::string realmTime = FormatRealmTime(event.TimerMs);

        std::string message = event.Warning;
        ReplaceAll(message, "{player}", event.PendingAnnouncer);
        ReplaceAll(message, "{buff}", event.Label);
        ReplaceAll(message, "{time}", realmTime);
        ReplaceAll(message, "{minutes}", std::to_string(minutes));
        ReplaceAll(message, "{duration}", FormatMinutes(minutes));

        SendFactionMessage(event.Faction, message);

        if (_debug)
            LOG_INFO("module", "WorldBuffBots: warned that {} drops at {} realm time ({} minutes out)",
                event.Label, realmTime, minutes);
    }

    // The client's realm clock is built from the server's local time (see
    // ByteBuffer::AppendPackedTime), so formatting local time here matches the
    // clock players actually read in game.
    std::string FormatRealmTime(uint32 fromNowMs) const
    {
        Seconds firesAt = GameTime::GetGameTime() + Seconds(fromNowMs / IN_MILLISECONDS);
        return Acore::Time::TimeToTimestampStr(firesAt, _timeFormat);
    }

    void FireEvent(WorldBuffEvent const& event)
    {
        std::string announcement = FormatAnnouncement(event, event.PendingAnnouncer);

        SendFactionMessage(event.Faction, announcement);

        uint32 appliedCount = 0;
        for (uint32 areaId : event.TargetAreaIds)
            appliedCount += ApplySpellToArea(event.SpellId, areaId, event.Faction);

        if (event.SpellId == SPELL_WARCHIEFS_BLESSING && _warchiefCrossroads)
            _delayedCasts.push_back({ event.SpellId, AREA_THE_CROSSROADS, event.Faction, 10 * IN_MILLISECONDS });

        LOG_INFO("module", "WorldBuffBots: fired {} using announcer '{}' and applied {} immediate buffs",
            event.Label, event.PendingAnnouncer, appliedCount);
    }

    void UpdateDelayedCasts(uint32 diff)
    {
        for (auto itr = _delayedCasts.begin(); itr != _delayedCasts.end();)
        {
            if (itr->TimerMs <= diff)
            {
                uint32 appliedCount = ApplySpellToArea(itr->SpellId, itr->AreaId, itr->Faction);

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

    void SendFactionMessage(BuffFaction faction, std::string const& message) const
    {
        if (faction == BuffFaction::Both)
        {
            sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, message);
            return;
        }

        sWorldSessionMgr->DoForAllOnlinePlayers([&](Player* player)
        {
            if (!player || !player->IsInWorld() || !MatchesFaction(player, faction))
                return;

            sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, message, player);
        });
    }

    uint32 ApplySpellToArea(uint32 spellId, uint32 areaId, BuffFaction faction) const
    {
        uint32 appliedCount = 0;

        sWorldSessionMgr->DoForAllOnlinePlayers([&](Player* player)
        {
            if (!IsEligibleTarget(player) || !IsInAreaOrZone(player, areaId))
                return;

            if (_restrictBuffToFaction && !MatchesFaction(player, faction))
                return;

            player->CastSpell(player, spellId, true);
            ++appliedCount;
        });

        return appliedCount;
    }

    std::string SelectAnnouncerName(BuffFaction faction) const
    {
        switch (faction)
        {
            case BuffFaction::Alliance:
                return PickRandomName(ALLIANCE_ANNOUNCER_NAMES);
            case BuffFaction::Horde:
                return PickRandomName(HORDE_ANNOUNCER_NAMES);
            case BuffFaction::Both:
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
    bool _restrictBuffToFaction = true;
    bool _warchiefCrossroads = true;
    uint32 _baseMinutes = 90;
    uint32 _varianceMinutes = 60;
    uint32 _initialMinMinutes = 30;
    uint32 _initialMaxMinutes = 150;
    uint32 _warningLeadMs = 10 * MS_PER_MINUTE;
    std::string _timeFormat = "%H:%M";
};

void AddWorldBuffBotsScripts()
{
    new WorldBuffBotsWorldScript();
}
