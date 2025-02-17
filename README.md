# AzerothCore Module: Bot Level Brackets
==========================================

Overview
--------
The Bot Level Brackets module for AzerothCore ensures an even spread of player bots across configurable level ranges (brackets). It periodically monitors bot levels and automatically adjusts them by transferring bots from overpopulated brackets to those with a deficit. During adjustments, bot levels are reset, equipped items are destroyed, pets are removed, and auto-maintenance actions are executed.

Features
--------
• **Configurable Level Brackets:**  
   Define nine distinct level brackets with customizable lower and upper bounds:
   - 1-9  
   - 10-19  
   - 20-29  
   - 30-39  
   - 40-49  
   - 50-59  
   - 60-69  
   - 70-79  
   - 80

• **Desired Percentage Distribution:**  
   Set target percentages for the number of bots within each level bracket.

• **Dynamic Bot Adjustment:**  
   Automatically reassign bots from brackets with a surplus to those with a deficit.

• **Death Knight Level Safeguard:**  
   Ensures that bots of the Death Knight class are never assigned a level below 55, guaranteeing they only appear in higher brackets.

• **Auto Maintenance Execution:**  
   Executes the AutoMaintenanceOnLevelupAction after adjusting a bot’s level to ensure proper reinitialization.

• **Equipment and Pet Reset:**  
   Destroys all equipped items and removes any pet during a level adjustment.

• **Support for Random Bots:**  
   Applies exclusively to bots managed by RandomPlayerbotMgr.

• **Debug Mode:**  
   Provides detailed logging to aid in monitoring and troubleshooting module operations.

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
Customize the module’s behavior by editing the `mod_player_bot_level_brackets.conf` file:

Setting                          | Description                                                                                                    | Default | Valid Values
-------------------------------- | -------------------------------------------------------------------------------------------------------------- | ------- | --------------------
BotLevelBrackets.DebugMode       | Enables detailed debug logging for module operations.                                                        | 0       | 0 (off) / 1 (on)
BotLevelBrackets.CheckFrequency  | Frequency (in seconds) for performing the bot bracket distribution check.                                      | 300     | Positive Integer
BotLevelBrackets.Range1Pct       | Desired percentage of bots in level bracket 1-9.                                                               | 11      | 0-100
BotLevelBrackets.Range2Pct       | Desired percentage of bots in level bracket 10-19.                                                             | 11      | 0-100
BotLevelBrackets.Range3Pct       | Desired percentage of bots in level bracket 20-29.                                                             | 11      | 0-100
BotLevelBrackets.Range4Pct       | Desired percentage of bots in level bracket 30-39.                                                             | 11      | 0-100
BotLevelBrackets.Range5Pct       | Desired percentage of bots in level bracket 40-49.                                                             | 11      | 0-100
BotLevelBrackets.Range6Pct       | Desired percentage of bots in level bracket 50-59.                                                             | 11      | 0-100
BotLevelBrackets.Range7Pct       | Desired percentage of bots in level bracket 60-69.                                                             | 11      | 0-100
BotLevelBrackets.Range8Pct       | Desired percentage of bots in level bracket 70-79.                                                             | 11      | 0-100
BotLevelBrackets.Range9Pct       | Desired percentage of bots in level bracket 80.                                                                | 12      | 0-100

*Note: The sum of all bracket percentages must equal 100.*

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
