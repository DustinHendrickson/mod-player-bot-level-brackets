# AzerothCore Module: Bot Level Brackets

<p align="center">
  <img src="./icon.png" alt="Bot Level Brackets Icon" title="Bot Level Brackets Icon">
</p>

> **Disclaimer:** This module requires the [Playerbots module](https://github.com/liyunfan1223/mod-playerbots). Please ensure that the Playerbots module is installed and running before using this module.

Overview
--------
The Bot Level Brackets module for AzerothCore ensures an even spread of player bots across configurable level ranges (brackets). It periodically monitors bot levels and automatically adjusts them by transferring bots from overpopulated brackets to those with a deficit. During adjustments, bots will be run through the normal Playerbots Randomize function, clearing and restoring them based on their new level. Bots that are not immediately safe for level reset (for example, those in combat or engaged in other activities) are flagged for pending adjustment and processed later when they become safe. Additionally, Death Knight bots are safeguarded to never be assigned a level below 55.

Features
--------
- **Configurable Level Brackets:**  
  Define nine distinct level brackets with configurable lower and upper bounds:
  - 1-9 , 10-19 , 20-29 , 30-39 , 40-49 , 50-59 , 60-69 , 70-79 , 80

- **Faction-Specific Configuration:**  
  Separate configurations for Alliance and Horde bots allow individual control over desired bot percentages within each bracket.

- **Desired Percentage Distribution:**  
  Target percentages can be set for the number of bots within each level bracket. The sum of percentages for each faction must equal 100.

- **Dynamic Bot Adjustment:**  
  Bots in overpopulated brackets are automatically adjusted to a random level within a bracket with a deficit. Adjustments include resetting XP, removing equipped items, trade skills, learned spells, quests, and active auras, and dismissing pets.

- **Death Knight Level Safeguard:**  
  Bots of the Death Knight class are enforced a minimum level of 55, ensuring they are only assigned to higher brackets.

- **Support for Random Bots:**  
  The module applies exclusively to bots managed by RandomPlayerbotMgr.

- **Dynamic Real Player Weighting with Inverse Scaling:**  
  When dynamic distribution is enabled, the module uses a configurable weight multiplier (set via `BotLevelBrackets.RealPlayerWeight`) to boost each real player's contribution to the desired distribution. This weight is further scaled inversely by the total number of real players online, ensuring that when few players are active, each player's impact on the bot distribution is significantly increased.  
  **Note:** The `RealPlayerWeight` option only takes effect when `BotLevelBrackets.UseDynamicDistribution` is enabled.

- **Dynamic Distribution Toggle:**  
  Enable or disable the dynamic recalculation of bot distribution percentages based on the number of non-bot players in each level bracket via the `BotLevelBrackets.UseDynamicDistribution` option.

- **Debug Mode:**  
  An optional debug mode provides detailed logging for monitoring bot adjustments and troubleshooting module operations.

### Dynamic Real Player Weighting and Scaling

When dynamic distribution is enabled (`BotLevelBrackets.UseDynamicDistribution`), the module recalculates the desired bot percentages for each level bracket based on the number of non-bot players present. 

The total weight across all brackets is computed, and each bracket’s desired percentage is derived from its share of that total. This ensures that while more players in a bracket increase its weight, each additional player contributes progressively less.


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

Setting                                   | Description                                                                                                                      | Default | Valid Values
------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------|---------|--------------------
BotLevelBrackets.DebugMode                | Enables detailed debug logging for module operations.                                                                            | 0       | 0 (off) / 1 (on)
BotLevelBrackets.CheckFrequency           | Frequency (in seconds) for performing the bot bracket distribution check.                                                        | 300     | Positive Integer
BotLevelBrackets.CheckFlaggedFrequency    | Frequency (in seconds) at which the bot level reset is performed for flagged bots that failed safety checks initially.              | 15      | Positive Integer
BotLevelBrackets.RealPlayerWeight         | Multiplier applied to each real player's contribution in their level bracket. This value is further scaled inversely by the total number of real players online. **Only active if dynamic distribution is enabled.** | 1.0     | Floating point number
BotLevelBrackets.UseDynamicDistribution    | Enables dynamic recalculation of bot distribution percentages based on the number of non-bot players in each bracket.              | 0       | 0 (off) / 1 (on)

### Alliance Level Brackets Configuration
*The percentages below must sum to 100.*

Setting                                    | Description                                                   | Default | Valid Values
-------------------------------------------|---------------------------------------------------------------|---------|--------------------
BotLevelBrackets.Alliance.Range1Pct         | Desired percentage of Alliance bots within level range 1-9.    | 12      | 0-100
BotLevelBrackets.Alliance.Range2Pct         | Desired percentage of Alliance bots within level range 10-19.  | 11      | 0-100
BotLevelBrackets.Alliance.Range3Pct         | Desired percentage of Alliance bots within level range 20-29.  | 11      | 0-100
BotLevelBrackets.Alliance.Range4Pct         | Desired percentage of Alliance bots within level range 30-39.  | 11      | 0-100
BotLevelBrackets.Alliance.Range5Pct         | Desired percentage of Alliance bots within level range 40-49.  | 11      | 0-100
BotLevelBrackets.Alliance.Range6Pct         | Desired percentage of Alliance bots within level range 50-59.  | 11      | 0-100
BotLevelBrackets.Alliance.Range7Pct         | Desired percentage of Alliance bots within level range 60-69.  | 11      | 0-100
BotLevelBrackets.Alliance.Range8Pct         | Desired percentage of Alliance bots within level range 70-79.  | 11      | 0-100
BotLevelBrackets.Alliance.Range9Pct         | Desired percentage of Alliance bots at level 80.               | 11      | 0-100

### Horde Level Brackets Configuration
*The percentages below must sum to 100.*

Setting                                    | Description                                                   | Default | Valid Values
-------------------------------------------|---------------------------------------------------------------|---------|--------------------
BotLevelBrackets.Horde.Range1Pct           | Desired percentage of Horde bots within level range 1-9.      | 12      | 0-100
BotLevelBrackets.Horde.Range2Pct           | Desired percentage of Horde bots within level range 10-19.    | 11      | 0-100
BotLevelBrackets.Horde.Range3Pct           | Desired percentage of Horde bots within level range 20-29.    | 11      | 0-100
BotLevelBrackets.Horde.Range4Pct           | Desired percentage of Horde bots within level range 30-39.    | 11      | 0-100
BotLevelBrackets.Horde.Range5Pct           | Desired percentage of Horde bots within level range 40-49.    | 11      | 0-100
BotLevelBrackets.Horde.Range6Pct           | Desired percentage of Horde bots within level range 50-59.    | 11      | 0-100
BotLevelBrackets.Horde.Range7Pct           | Desired percentage of Horde bots within level range 60-69.    | 11      | 0-100
BotLevelBrackets.Horde.Range8Pct           | Desired percentage of Horde bots within level range 70-79.    | 11      | 0-100
BotLevelBrackets.Horde.Range9Pct           | Desired percentage of Horde bots at level 80.                 | 11      | 0-100

Debugging
---------
To enable detailed debug logging, update the configuration file:

    BotLevelBrackets.DebugMode = 1

This setting outputs logs detailing bot level adjustments, percentages and distribution to the server console.

License
-------
This module is released under the GNU GPL v2 license, consistent with AzerothCore's licensing model.

Contribution
------------
Created by Dustin Hendrickson.

Pull requests and issues are welcome. Please ensure that contributions adhere to AzerothCore's coding standards.
