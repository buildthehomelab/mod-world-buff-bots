# World Buff Simulator (`mod-world-buff-bots`)

Created by Rockhopper1776

Drop-in module for standard AzerothCore. It does not require `mod-playerbots` or the Playerbots core fork. It simulates classic world buff turn-ins on independent randomized timers:

- Warchief's Blessing, spell `16609`
- Rallying Cry of the Dragonslayer, spell `22888`
- Spirit of Zandalar, spell `24425`

Each timer defaults to `90 +/- 60` minutes, so every buff rerolls independently between 30 and 150 minutes after it fires. Timers are not persistent; they reroll on worldserver startup and config reload.

## Behavior

When a buff fires, the module:

1. Chooses a random generated announcer name from a built-in faction-appropriate pool:
   - Horde for Warchief's Blessing
   - Alliance for Rallying Cry of the Dragonslayer
   - Either faction for Spirit of Zandalar
2. Sends a global announcement using that name.
3. Applies the actual buff spell to alive, non-GM players in the relevant area or zone.

Warchief's Blessing also applies to the Crossroads 10 seconds later by default, matching AzerothCore's existing Thrall reward behavior. You can disable that in the config.

## Install

1. Copy `mod-world-buff-bots` into your AzerothCore `modules/` directory.
2. Re-run CMake and rebuild worldserver.
3. Edit `mod_world_buff_bots.conf` if you want different timers, announcements, or enabled buffs. It is normally installed under `env/dist/etc/modules` on Unix-like installations and `env/dist/configs/modules` on Windows.

### Upgrading from the Playerbots-based version

The settings `WorldBuffBots.BotFallbackLevel` and `WorldBuffBots.FallbackAnnouncerName` are no longer used. Remove them from an existing `mod_world_buff_bots.conf`; the generated name pools work even when no players or bots are online.

For quick testing, set:

```ini
WorldBuffBots.InitialMinMinutes = 1
WorldBuffBots.InitialMaxMinutes = 1
WorldBuffBots.BaseMinutes = 1
WorldBuffBots.VarianceMinutes = 0
WorldBuffBots.Debug = 1
```

## Notes

This module intentionally simulates the reward result directly instead of forcing quest completion. It does not create a character for the generated announcer name, and it does not alter quest status, inventory, or NPC state.

# GNU Affero General Public License v3.0
