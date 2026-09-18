# World Buff Simulator (`mod-world-buff-bots`)

Created by Rockhopper1776

Drop-in module for standard AzerothCore. It does not require `mod-playerbots` or the Playerbots core fork. It simulates classic world buff turn-ins on independent randomized timers:

- Warchief's Blessing, spell `16609` — Horde
- Rallying Cry of the Dragonslayer, spell `22888` — Alliance
- Spirit of Zandalar, spell `24425` — both factions

Each timer defaults to `90 +/- 60` minutes, so every buff rerolls independently between 30 and 150 minutes after it fires. Timers are not persistent; they reroll on worldserver startup and config reload.

## Behavior

Each buff runs a two-step cycle.

**10 minutes before it fires**, the module sends a heads-up so players have time to travel and catch the buff:

```
Azgora is carrying Rend Blackhand's head to Orgrimmar. Warchief's Blessing in about 10 minutes!
```

**When the timer expires**, it announces the turn-in and applies the actual buff spell to alive, non-GM players in the relevant area or zone:

```
Rend Blackhand has fallen! Thrall has granted Warchief's Blessing in honor of Azgora.
```

The announcer name is a random generated name from a built-in faction-appropriate pool. It is chosen when the timer is armed, so the warning and the announcement always name the same character.

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
| `WorldBuffBots.BaseMinutes` / `.VarianceMinutes` | `90` / `60` | Reroll window between firings. |
| `WorldBuffBots.<Buff>.Warning` | per buff | Warning text. Leave empty to suppress that buff's warning. |
| `WorldBuffBots.<Buff>.Announcement` | per buff | Turn-in text. |

Placeholders usable in both messages:

- `{player}` — the generated announcer name
- `{buff}` — the buff name, e.g. `Warchief's Blessing`
- `{time}` — time remaining, e.g. `10 minutes` (warnings only)
- `{minutes}` — time remaining as a bare number (warnings only)

If a rerolled timer happens to land shorter than `WarningMinutes`, the warning still goes out on the next world update and reports the time actually remaining rather than a stale 10 minutes.

## Install

1. Copy `mod-world-buff-bots` into your AzerothCore `modules/` directory.
2. Re-run CMake and rebuild worldserver.
3. Edit `mod_world_buff_bots.conf` if you want different timers, announcements, or enabled buffs. It is normally installed under `env/dist/etc/modules` on Unix-like installations and `env/dist/configs/modules` on Windows.

### Upgrading from the Playerbots-based version

The settings `WorldBuffBots.BotFallbackLevel` and `WorldBuffBots.FallbackAnnouncerName` are no longer used. Remove them from an existing `mod_world_buff_bots.conf`; the generated name pools work even when no players or bots are online.

For quick testing, set:

```ini
WorldBuffBots.InitialMinMinutes = 3
WorldBuffBots.InitialMaxMinutes = 3
WorldBuffBots.BaseMinutes = 3
WorldBuffBots.VarianceMinutes = 0
WorldBuffBots.WarningMinutes = 2
WorldBuffBots.Debug = 1
```

Keep `WarningMinutes` below the timer length or the warning will fire almost immediately after each reroll.

## Notes

This module intentionally simulates the reward result directly instead of forcing quest completion. It does not create a character for the generated announcer name, and it does not alter quest status, inventory, or NPC state.

# GNU Affero General Public License v3.0
