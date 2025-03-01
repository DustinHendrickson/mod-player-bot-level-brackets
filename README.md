# AzerothCore Module: Bot Level Brackets

<p align="center">
  <img src="./icon.png" alt="Bot Level Brackets Icon" title="Bot Level Brackets Icon">
</p>

> **Disclaimer:** This module requires the [Playerbots module](https://github.com/liyunfan1223/mod-playerbots). Please ensure that the Playerbots module is installed and running before using this module.

Overview
--------
The Bot Level Brackets module for AzerothCore ensures an even spread of player bots across configurable level ranges (brackets). It periodically monitors bot levels and automatically adjusts them by transferring bots from overpopulated brackets to those with a deficit. During adjustments, bots are run through the normal Playerbots Randomize function, clearing and restoring them based on their new level. Bots that are not immediately safe for level reset (for example, those in combat or engaged in other activities) are flagged for pending adjustment and processed later when they become safe. Additionally, Death Knight bots are safeguarded to never be assigned a level below 55.

Features
--------
- **Configurable Faction-Specific Level Brackets:**  
  Define nine distinct level brackets for Alliance and Horde bots with configurable lower and upper bounds:
  - 1-9, 10-19, 20-29, 30-39, 40-49, 50-59, 60-69, 70-79, 80

- **Desired Percentage Distribution:**  
  Target percentages can be set for the number of bots within each level bracket. The sum of percentages for each faction must equal 100. If it does not, the system will balance out the remaining percentages automatically.

- **Dynamic Bot Adjustment:**  
  Bots in overpopulated brackets are automatically adjusted to a random level within a bracket with a deficit. Adjustments include resetting XP, removing equipped items, trade skills, learned spells, quests, and active auras, and dismissing pets.

- **Death Knight Level Safeguard:**  
  Death Knight bots are enforced a minimum level of 55, ensuring they are only assigned to higher brackets.

- **Support for Random Bots:**  
  The module applies exclusively to bots managed by the RandomPlayerbotMgr.

- **Dynamic Distribution Toggle:**  
  Enable or disable the dynamic recalculation of bot distribution percentages based on the number of non-bot players in each level bracket via the `BotLevelBrackets.UseDynamicDistribution` option.

- **Dynamic Real Player Weighting with Inverse Scaling:**  
  When dynamic distribution is enabled, the module uses a configurable weight multiplier (set via `BotLevelBrackets.RealPlayerWeight`) to boost each real player's contribution to the desired distribution. This weight is further scaled inversely by the total number of real players online, ensuring that when few players are active, each player's impact on the bot distribution is significantly increased.  
  **Note:** The `RealPlayerWeight` option only takes effect when `BotLevelBrackets.UseDynamicDistribution` is enabled.

- **Guild Bot Exclusion:**  
  When enabled via the new configuration option `BotLevelBrackets.IgnoreGuildBotsWithRealPlayers` (default enabled), bots that are in a guild with at least one real (non-bot) player online are excluded from bot bracket calculations. These bots are not counted in the totals, nor are they subject to level changes or flagged for pending reset.
 > **NOTE:** At this time Guild Bot Exclusion only works with **online** real players. I'm investigating how to accomplish the same thing even if the real player is offline.

- **Debug Mode:**  
  Optional debug modes (full and lite) provide detailed logging for monitoring bot adjustments and troubleshooting module operations.

### Minimum and Maximum Bot Level Support

This module now supports setting a minimum and maximum level for random bots via the Playerbots `playerbots.conf` options:

- **AiPlayerbot.RandomBotMinLevel:**  
  Sets the minimum level allowed for random bots. The default value is 1.

- **AiPlayerbot.RandomBotMaxLevel:**  
  Sets the maximum level allowed for random bots. The default value is 80.

> **Warning:** If you configure the maximum bot level to a value below 55, ensure that Death Knight bots are disabled. The module enforces a minimum level of 55 for Death Knight bots; therefore, setting the maximum level under 55 would conflict with this safeguard and could lead to unintended behavior and Death Knight bots not moving brackets.

Installation
------------
1. **Clone the Module**  
   Ensure the AzerothCore Playerbots fork is installed and running. Clone the module into your AzerothCore modules directory:
   
       cd /path/to/azerothcore/modules
       git clone https://github.com/DustinHendrickson/mod-player-bot-level-brackets.git

2. **Recompile AzerothCore**  
   Rebuild the project with the new module:
   
       cd /path/to/azerothcore
       mkdir build && cd build
       cmake ..
       make -j$(nproc)

3. **Configure the Module**  
   Rename the configuration file:
   
       mv /path/to/azerothcore/modules/mod_player_bot_level_brackets.conf.dist /path/to/azerothcore/modules/mod_player_bot_level_brackets.conf

4. **Restart the Server**  
   Launch the world server:
   
       ./worldserver

Configuration Options
---------------------
Customize the module’s behavior by editing the `mod_player_bot_level_brackets.conf` file. The configuration options are separated for Alliance and Horde bots:

### Global Settings

Setting                                      | Description                                                                                                                      | Default | Valid Values
---------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------|---------|--------------------
BotLevelBrackets.Enabled                     | Enables the module.                                                                                                              | 1       | 0 (off) / 1 (on)
BotLevelBrackets.FullDebugMode               | Enables full debug logging for the Bot Level Brackets module.                                                                    | 0       | 0 (off) / 1 (on)
BotLevelBrackets.LiteDebugMode               | Enables lite debug logging for the Bot Level Brackets module.                                                                    | 0       | 0 (off) / 1 (on)
BotLevelBrackets.CheckFrequency              | Frequency (in seconds) at which the bot level distribution check is performed.                                                  | 300     | Positive Integer
BotLevelBrackets.CheckFlaggedFrequency       | Frequency (in seconds) at which the bot level reset is performed for flagged bots that initially failed safety checks.             | 15      | Positive Integer
BotLevelBrackets.UseDynamicDistribution      | Enables dynamic recalculation of bot distribution percentages based on the number of non-bot players present in each bracket.      | 0       | 0 (off) / 1 (on)
BotLevelBrackets.RealPlayerWeight            | Multiplier applied to each real player's contribution in their level bracket. **Active only if dynamic distribution is enabled.** | 1.0     | Floating point number
**BotLevelBrackets.IgnoreGuildBotsWithRealPlayers** | When enabled, bots in a guild with at least one real (non-bot) player online are excluded from bot bracket calculations and will not be level changed or flagged. | 1       | 0 (disabled) / 1 (enabled)

### Alliance Level Brackets Configuration
*The percentages below must sum to 100.*

Setting                                     | Description                                                   | Default | Valid Values
--------------------------------------------|---------------------------------------------------------------|---------|--------------------
BotLevelBrackets.Alliance.Range1Pct          | Desired percentage of Alliance bots within level range 1-9.    | 12      | 0-100
BotLevelBrackets.Alliance.Range2Pct          | Desired percentage of Alliance bots within level range 10-19.  | 11      | 0-100
BotLevelBrackets.Alliance.Range3Pct          | Desired percentage of Alliance bots within level range 20-29.  | 11      | 0-100
BotLevelBrackets.Alliance.Range4Pct          | Desired percentage of Alliance bots within level range 30-39.  | 11      | 0-100
BotLevelBrackets.Alliance.Range5Pct          | Desired percentage of Alliance bots within level range 40-49.  | 11      | 0-100
BotLevelBrackets.Alliance.Range6Pct          | Desired percentage of Alliance bots within level range 50-59.  | 11      | 0-100
BotLevelBrackets.Alliance.Range7Pct          | Desired percentage of Alliance bots within level range 60-69.  | 11      | 0-100
BotLevelBrackets.Alliance.Range8Pct          | Desired percentage of Alliance bots within level range 70-79.  | 11      | 0-100
BotLevelBrackets.Alliance.Range9Pct          | Desired percentage of Alliance bots at level 80.               | 11      | 0-100

### Horde Level Brackets Configuration
*The percentages below must sum to 100.*

Setting                                    | Description                                                   | Default | Valid Values
-------------------------------------------|---------------------------------------------------------------|---------|--------------------
BotLevelBrackets.Horde.Range1Pct            | Desired percentage of Horde bots within level range 1-9.      | 12      | 0-100
BotLevelBrackets.Horde.Range2Pct            | Desired percentage of Horde bots within level range 10-19.    | 11      | 0-100
BotLevelBrackets.Horde.Range3Pct            | Desired percentage of Horde bots within level range 20-29.    | 11      | 0-100
BotLevelBrackets.Horde.Range4Pct            | Desired percentage of Horde bots within level range 30-39.    | 11      | 0-100
BotLevelBrackets.Horde.Range5Pct            | Desired percentage of Horde bots within level range 40-49.    | 11      | 0-100
BotLevelBrackets.Horde.Range6Pct            | Desired percentage of Horde bots within level range 50-59.    | 11      | 0-100
BotLevelBrackets.Horde.Range7Pct            | Desired percentage of Horde bots within level range 60-69.    | 11      | 0-100
BotLevelBrackets.Horde.Range8Pct            | Desired percentage of Horde bots within level range 70-79.    | 11      | 0-100
BotLevelBrackets.Horde.Range9Pct            | Desired percentage of Horde bots at level 80.                 | 11      | 0-100

Debugging
---------
To enable detailed debug logging, update the configuration file:

    BotLevelBrackets.FullDebugMode = 1
    BotLevelBrackets.LiteDebugMode = 1

Choose one of these debug modes to output logs detailing bot level adjustments, percentages, and distribution to the server console.

Troubleshooting
---------------
> **Bots are not randomizing their levels within the range brackets.**  
> Ensure that in your `playerbots.conf` the option `AiPlayerbot.DisableRandomLevels` is set to false. Otherwise, bots will be reset to the fixed level specified in your Playerbots configuration.

License
-------
This module is released under the GNU GPL v2 license, consistent with AzerothCore's licensing model.

Contribution
------------
Created by Dustin Hendrickson.

Pull requests and issues are welcome. Please ensure that contributions adhere to AzerothCore's coding standards.
