# World Buff Simulator
# mod-world-buff-bots
# Created by Rockhopper1776

Drop-in AzerothCore module for a `mod-playerbots` branch. It simulates classic world buff turn-ins on independent randomized timers:

- Warchief's Blessing, spell `16609`
- Rallying Cry of the Dragonslayer, spell `22888`
- Spirit of Zandalar, spell `24425`

Each timer defaults to `90 +/- 60` minutes, so every buff rerolls independently between 30 and 150 minutes after it fires. Timers are not persistent; they reroll on worldserver startup and config reload.

## Behavior

When a buff fires, the module:

1. Chooses an announcer name from online bots in the relevant area:
   - Orgrimmar for Warchief's Blessing
   - Stormwind for Rallying Cry of the Dragonslayer
   - Stranglethorn Vale for Spirit of Zandalar
2. Uses the highest-level bot in that area, choosing randomly among ties. If every local candidate is level 60, it chooses randomly among them.
3. If no local bot exists, chooses a random online level 60 bot.
4. If no level 60 bot exists, chooses the highest-level online bot.
5. If no online bot exists at all, uses the configured fallback name.
6. Sends a global announcement.
7. Applies the actual buff spell to alive, non-GM players in the relevant area or zone.

Warchief's Blessing also applies to the Crossroads 10 seconds later by default, matching AzerothCore's existing Thrall reward behavior. You can disable that in the config.

## Install

1. Copy `mod-world-buff-bots` into your AzerothCore `modules/` directory.
2. Re-run CMake and rebuild worldserver.
3. Edit the mod_world_buff_bots.conf in your env/dist/etc/modules folder if you want different timers, announcements, or enabled buffs.

For quick testing, set:

```ini
WorldBuffBots.InitialMinMinutes = 1
WorldBuffBots.InitialMaxMinutes = 1
WorldBuffBots.BaseMinutes = 1
WorldBuffBots.VarianceMinutes = 0
WorldBuffBots.Debug = 1
```

## Notes

This module intentionally simulates the reward result directly instead of forcing quest completion. It does not alter quest status, inventory, or NPC state.

# GNU Affero General Public License v3.0
