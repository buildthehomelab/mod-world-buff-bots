# World Buff Simulator (`mod-world-buff-bots`)

Created by Rockhopper1776

Drop-in module for standard AzerothCore. It does not require `mod-playerbots` or the Playerbots core fork. It simulates classic world buff turn-ins:

- Warchief's Blessing, spell `16609` — Horde
- Rallying Cry of the Dragonslayer, spell `22888` — Alliance
- Spirit of Zandalar, spell `24425` — both factions

All three share **one cycle**, so the Horde and Alliance city turn-ins always land at the same moment and neither faction gets a head start. Spirit of Zandalar fires 10 minutes later in that same cycle, giving anyone who caught a city buff time to travel down to Zul'Gurub for it.

The cycle defaults to `90 +/- 60` minutes between city turn-ins, so it rerolls between 30 and 150 minutes. Timers are not persistent; they reroll on worldserver startup and config reload.

## Behavior

A cycle runs like this, with default settings:

| Time | What happens |
| --- | --- |
| `T - 10 min` | Horde and Alliance both get their warning, naming the realm time `T` |
| `T` | Warchief's Blessing drops in Orgrimmar, Rallying Cry drops in Stormwind, and the ZG warning goes out |
| `T + 10 min` | Spirit of Zandalar drops in Stranglethorn Vale |
| `T + 90 min` | Next cycle's city turn-in |

The ZG warning lands exactly as the city buffs drop, which is what gives players the full 10 minutes to get down there.

Every buff announces itself twice. **Before it fires**, a heads-up goes out naming the realm clock time the buff will drop, so players can decide whether they can make it:

```
Azgora is carrying Rend Blackhand's head to Orgrimmar. Warchief's Blessing drops at 20:35 realm time!
```

Realm time is the worldserver's local time, which is exactly what the in-game clock shows players — the module formats it the same way the core builds the client's clock.

**When it fires**, the module announces the turn-in and applies the actual buff spell to alive, non-GM players in the relevant area or zone:

```
Rend Blackhand has fallen! Thrall has granted Warchief's Blessing in honor of Azgora.
```

The announcer name is a random generated name from a built-in faction-appropriate pool. It is chosen when the cycle is armed, so the warning and the announcement always name the same character.

The next cycle is measured from the city turn-in, not from Spirit of Zandalar, so changing the ZG offset delays that buff without stretching the gap between cycles.

### Faction targeting

Announcements are sent only to the faction that can act on them:

| Buff | Announced to | Buffs |
| --- | --- | --- |
| Warchief's Blessing | Horde | Horde in Orgrimmar (and the Crossroads) |
| Rallying Cry of the Dragonslayer | Alliance | Alliance in Stormwind |
| Spirit of Zandalar | Both | Both in Stranglethorn Vale |

By default the buff itself is also restricted to that faction, so an Alliance player standing in Orgrimmar is not silently buffed by an event they never saw announced. Set `WorldBuffBots.RestrictBuffToFaction = 0` to buff everyone in the target area while keeping the announcements faction specific.

Warchief's Blessing also applies to the Crossroads 10 seconds later by default, matching AzerothCore's existing Thrall reward behavior. You can disable that in the config.

## Configuration

Key settings in `mod_world_buff_bots.conf`:

| Setting | Default | Meaning |
| --- | --- | --- |
| `WorldBuffBots.WarningMinutes` | `10` | Lead time on the heads-up announcement. `0` disables warnings. |
| `WorldBuffBots.RestrictBuffToFaction` | `1` | Only buff players of the buff's own faction. |
| `WorldBuffBots.BaseMinutes` / `.VarianceMinutes` | `90` / `60` | Reroll window between city turn-ins. |
| `WorldBuffBots.Zandalar.OffsetMinutes` | `10` | How long after the city buffs ZG fires. |
| `WorldBuffBots.<Buff>.OffsetMinutes` | `0` | Per-buff offset within the cycle. |
| `WorldBuffBots.TimeFormat` | `%H:%M` | `strftime` format for `{time}`. Use `%I:%M %p` for a 12 hour clock. |
| `WorldBuffBots.<Buff>.Warning` | per buff | Warning text. Leave empty to suppress that buff's warning. |
| `WorldBuffBots.<Buff>.Announcement` | per buff | Turn-in text. |

Placeholders usable in both messages:

- `{player}` — the generated announcer name
- `{buff}` — the buff name, e.g. `Warchief's Blessing`
- `{time}` — realm clock time the buff drops, e.g. `20:35` (warnings only)
- `{duration}` — time remaining, e.g. `10 minutes` (warnings only)
- `{minutes}` — time remaining as a bare number (warnings only)

If you prefer the countdown phrasing, use `{duration}` instead:

```ini
WorldBuffBots.Warchief.Warning = {player} is carrying Rend Blackhand's head to Orgrimmar. Warchief's Blessing in about {duration}!
```

If a rerolled timer happens to land shorter than `WarningMinutes`, the warning still goes out on the next world update and reports the time actually remaining rather than a stale 10 minutes.

## Install

1. Copy `mod-world-buff-bots` into your AzerothCore `modules/` directory.
2. Re-run CMake and rebuild worldserver.
3. Edit `mod_world_buff_bots.conf` if you want different timers, announcements, or enabled buffs. It is normally installed under `env/dist/etc/modules` on Unix-like installations and `env/dist/configs/modules` on Windows.

### Upgrading from the Playerbots-based version

The settings `WorldBuffBots.BotFallbackLevel` and `WorldBuffBots.FallbackAnnouncerName` are no longer used. Remove them from an existing `mod_world_buff_bots.conf`; the generated name pools work even when no players or bots are online.

For quick testing, set:

```ini
WorldBuffBots.InitialMinMinutes = 5
WorldBuffBots.InitialMaxMinutes = 5
WorldBuffBots.BaseMinutes = 5
WorldBuffBots.VarianceMinutes = 0
WorldBuffBots.WarningMinutes = 2
WorldBuffBots.Zandalar.OffsetMinutes = 2
WorldBuffBots.Debug = 1
```

Keep `WarningMinutes` below the cycle length or the warning will fire almost immediately after each reroll, and keep `Zandalar.OffsetMinutes` below it too or the ZG warning will arrive before the city buffs drop.

## Notes

This module intentionally simulates the reward result directly instead of forcing quest completion. It does not create a character for the generated announcer name, and it does not alter quest status, inventory, or NPC state.

# GNU Affero General Public License v3.0
