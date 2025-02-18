# AzerothCore Module: Bot Level Brackets
--------

Overview
--------
The Bot Level Brackets module for AzerothCore ensures an even spread of player bots across configurable level ranges (brackets). It periodically monitors bot levels and automatically adjusts them by transferring bots from overpopulated brackets to those with a deficit. During adjustments, bot levels are reset, equipped items are destroyed, trade skills are removed, quests are cleared, active auras are removed, pets are dismissed, and auto-maintenance actions are executed. Bots that are not immediately safe for level reset (for example, those in combat or engaged in other activities) are flagged for pending adjustment and processed later when they become safe. Additionally, Death Knight bots are safeguarded to never be assigned a level below 55.


Features
--------
- **Configurable Level Brackets:**  
  Define nine distinct level brackets with configurable lower and upper bounds:
  - 1-9  
  - 10-19  
  - 20-29  
  - 30-39  
  - 40-49  
  - 50-59  
  - 60-69  
  - 70-79  
  - 80

- **Faction-Specific Configuration:**  
  Separate configurations for Alliance and Horde bots allow individual control over desired bot percentages within each bracket.

- **Desired Percentage Distribution:**  
  Target percentages can be set for the number of bots within each level bracket. The sum of percentages for each faction must equal 100.

- **Dynamic Bot Adjustment:**  
  Bots in overpopulated brackets are automatically adjusted to a random level within a bracket with a deficit. Adjustments include resetting XP, removing equipped items, trade skills, learned spells, quests, and active auras, and dismissing pets.

- **Death Knight Level Safeguard:**  
  Bots of the Death Knight class are enforced a minimum level of 55, ensuring they are only assigned to higher brackets.

- **Automated Maintenance Execution:**  
  After a level change, the module executes the AutoMaintenanceOnLevelupAction to properly reinitialize the bot’s state.

- **Support for Random Bots:**  
  The module applies exclusively to bots managed by RandomPlayerbotMgr.

- **Debug Mode:**  
  An optional debug mode provides detailed logging for monitoring bot adjustments and troubleshooting module operations.

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

Setting                         | Description                                                                                  | Default | Valid Values
--------------------------------|----------------------------------------------------------------------------------------------|---------|--------------------
BotLevelBrackets.DebugMode      | Enables detailed debug logging for module operations.                                        | 0       | 0 (off) / 1 (on)
BotLevelBrackets.CheckFrequency | Frequency (in seconds) for performing the bot bracket distribution check.                    | 300     | Positive Integer

### Alliance Level Brackets Configuration
*The percentages below must sum to 100.*

Setting                                    | Description                                                  | Default | Valid Values
-------------------------------------------|--------------------------------------------------------------|---------|--------------------
BotLevelBrackets.Alliance.Range1Pct         | Desired percentage of Alliance bots within level range 1-9.   | 11      | 0-100
BotLevelBrackets.Alliance.Range2Pct         | Desired percentage of Alliance bots within level range 10-19. | 11      | 0-100
BotLevelBrackets.Alliance.Range3Pct         | Desired percentage of Alliance bots within level range 20-29. | 11      | 0-100
BotLevelBrackets.Alliance.Range4Pct         | Desired percentage of Alliance bots within level range 30-39. | 11      | 0-100
BotLevelBrackets.Alliance.Range5Pct         | Desired percentage of Alliance bots within level range 40-49. | 11      | 0-100
BotLevelBrackets.Alliance.Range6Pct         | Desired percentage of Alliance bots within level range 50-59. | 11      | 0-100
BotLevelBrackets.Alliance.Range7Pct         | Desired percentage of Alliance bots within level range 60-69. | 11      | 0-100
BotLevelBrackets.Alliance.Range8Pct         | Desired percentage of Alliance bots within level range 70-79. | 11      | 0-100
BotLevelBrackets.Alliance.Range9Pct         | Desired percentage of Alliance bots at level 80.              | 12      | 0-100

### Horde Level Brackets Configuration
*The percentages below must sum to 100.*

Setting                                | Description                                               | Default | Valid Values
---------------------------------------|-----------------------------------------------------------|---------|--------------------
BotLevelBrackets.Horde.Range1Pct         | Desired percentage of Horde bots within level range 1-9.  | 11      | 0-100
BotLevelBrackets.Horde.Range2Pct         | Desired percentage of Horde bots within level range 10-19.| 11      | 0-100
BotLevelBrackets.Horde.Range3Pct         | Desired percentage of Horde bots within level range 20-29.| 11      | 0-100
BotLevelBrackets.Horde.Range4Pct         | Desired percentage of Horde bots within level range 30-39.| 11      | 0-100
BotLevelBrackets.Horde.Range5Pct         | Desired percentage of Horde bots within level range 40-49.| 11      | 0-100
BotLevelBrackets.Horde.Range6Pct         | Desired percentage of Horde bots within level range 50-59.| 11      | 0-100
BotLevelBrackets.Horde.Range7Pct         | Desired percentage of Horde bots within level range 60-69.| 11      | 0-100
BotLevelBrackets.Horde.Range8Pct         | Desired percentage of Horde bots within level range 70-79.| 11      | 0-100
BotLevelBrackets.Horde.Range9Pct         | Desired percentage of Horde bots at level 80.             | 12      | 0-100

Debugging
---------
To enable detailed debug logging, update the configuration file:

    BotLevelBrackets.DebugMode = 1

This setting outputs logs detailing bot level adjustments, item destruction, pet removal, and the execution of auto-maintenance actions.

License
-------
This module is released under the GNU GPL v2 license, consistent with AzerothCore's licensing model.

Contribution
------------
Created by Dustin Hendrickson.

Pull requests and issues are welcome. Please ensure that contributions adhere to AzerothCore's coding standards.
