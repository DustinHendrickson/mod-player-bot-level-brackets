#include "ScriptMgr.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Log.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "Configuration/Config.h"
#include "AutoMaintenanceOnLevelupAction.h"
#include "Common.h"
#include <vector>
#include <cmath>
#include <utility>
#include <limits>
#include <algorithm>
#include "PlayerbotFactory.h"
#include "DatabaseEnv.h"
#include "QueryResult.h"
#include <string>
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "ArenaTeamMgr.h"

using namespace Acore::ChatCommands;

// Forward declarations.
class Guild;
static bool IsAlliancePlayerBot(Player* bot);
static bool IsHordePlayerBot(Player* bot);
static void ClampAndBalanceBrackets();

// -----------------------------------------------------------------------------
// LEVEL RANGE CONFIGURATION
// -----------------------------------------------------------------------------
// Same boundaries for both factions; only desired percentages differ.
struct LevelRangeConfig
{
    uint8 lower;         ///< Lower bound (inclusive)
    uint8 upper;         ///< Upper bound (inclusive)
    uint8 desiredPercent;///< Desired percentage of bots in this range
};

// Instead of a fixed constant, load the number of brackets from configuration.
static uint8 g_NumRanges = 9;

// Global variables to restrict bot levels.
static uint8 g_RandomBotMinLevel = 1;
static uint8 g_RandomBotMaxLevel = 80;

// Enable/disable the mod. Default is true.
static bool g_BotLevelBracketsEnabled = true;
// Ignore bots in guilds with a real player online. Default is true.
static bool g_IgnoreGuildBotsWithRealPlayers = true;
// Ignore bots in arena teams. Default is true.
static bool g_IgnoreArenaTeamBots = true;

// Use vectors to store the level ranges.
static std::vector<LevelRangeConfig> g_AllianceLevelRanges;
static std::vector<LevelRangeConfig> g_HordeLevelRanges;

static uint32 g_BotDistCheckFrequency = 300; // in seconds
static uint32 g_BotDistFlaggedCheckFrequency = 15; // in seconds
static uint32 g_GuildTrackerUpdateFrequency = 600; // in seconds (10 minutes)
static bool   g_BotDistFullDebugMode      = false;
static bool   g_BotDistLiteDebugMode      = false;
static bool   g_UseDynamicDistribution  = false;
static bool   g_IgnoreFriendListed = true;
static uint32 g_FlaggedProcessLimit = 5; // 0 = unlimited

// Real player weight to boost bracket contributions.
static float g_RealPlayerWeight = 1.0f;

// If true, synchronize bracket logic and real player influence across both factions.
// This option requires both Alliance and Horde bracket definitions to match perfectly.
// When enabled, all real players (regardless of faction) affect the dynamic distribution for both factions.
static bool g_SyncFactions = false;

// Array for character social list friends
std::vector<uint64> g_SocialFriendsList;

// Array for excluded bot names.
static std::vector<std::string> g_ExcludeBotNames;

// Array for real player guild IDs.
std::unordered_set<uint32> g_RealPlayerGuildIds;

// Persistent guild tracker - stores guild IDs that have real players (from database)
std::unordered_set<uint32> g_PersistentRealPlayerGuildIds;

struct PendingResetEntry
{
    ObjectGuid botGuid;
    int targetRange;
    const LevelRangeConfig* factionRanges;
};
static std::vector<PendingResetEntry> g_PendingLevelResets;


/**
 * @brief Loads and initializes the configuration for player bot level brackets.
 *
 * This function reads various configuration options related to bot level brackets from the server's configuration manager.
 * It sets global variables that control the behavior of bot distribution, debug modes, dynamic distribution, and level ranges
 * for both Alliance and Horde factions. The function also loads the minimum and maximum levels for random bots, the number of
 * level brackets, and the desired percentage of bots per bracket for each faction.
 *
 * If the SyncFactions option is enabled, the function ensures that the bracket definitions (number, lower, and upper bounds)
 * match exactly between Alliance and Horde. If a mismatch is detected, the server will log a fatal error and terminate.
 *
 * After loading and validating the configuration, the function calls ClampAndBalanceBrackets() to finalize bracket setup.
 */
static void LoadBotLevelBracketsConfig()
{
    g_BotLevelBracketsEnabled = sConfigMgr->GetOption<bool>("BotLevelBrackets.Enabled", true);
    g_IgnoreGuildBotsWithRealPlayers = sConfigMgr->GetOption<bool>("BotLevelBrackets.IgnoreGuildBotsWithRealPlayers", true);
    g_IgnoreArenaTeamBots = sConfigMgr->GetOption<bool>("BotLevelBrackets.IgnoreArenaTeamBots", true);
    
    g_BotDistFullDebugMode = sConfigMgr->GetOption<bool>("BotLevelBrackets.FullDebugMode", false);
    g_BotDistLiteDebugMode = sConfigMgr->GetOption<bool>("BotLevelBrackets.LiteDebugMode", false);
    g_BotDistCheckFrequency = sConfigMgr->GetOption<uint32>("BotLevelBrackets.CheckFrequency", 300);
    g_BotDistFlaggedCheckFrequency = sConfigMgr->GetOption<uint32>("BotLevelBrackets.CheckFlaggedFrequency", 15);
    g_GuildTrackerUpdateFrequency = sConfigMgr->GetOption<uint32>("BotLevelBrackets.GuildTrackerUpdateFrequency", 600);
    g_UseDynamicDistribution = sConfigMgr->GetOption<bool>("BotLevelBrackets.Dynamic.UseDynamicDistribution", false);
    g_RealPlayerWeight = sConfigMgr->GetOption<float>("BotLevelBrackets.Dynamic.RealPlayerWeight", 1.0f);
    g_SyncFactions = sConfigMgr->GetOption<bool>("BotLevelBrackets.Dynamic.SyncFactions", false);
    g_IgnoreFriendListed = sConfigMgr->GetOption<bool>("BotLevelBrackets.IgnoreFriendListed", true);
    g_FlaggedProcessLimit = sConfigMgr->GetOption<uint32>("BotLevelBrackets.FlaggedProcessLimit", 5);

    std::string excludeNames = sConfigMgr->GetOption<std::string>("BotLevelBrackets.ExcludeNames", "");
    g_ExcludeBotNames.clear();
    std::istringstream f(excludeNames);
    std::string s;
    while (getline(f, s, ',')) {
        s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
        if (!s.empty()) {
            g_ExcludeBotNames.push_back(s);
        }
    }

    // Load the bot level restrictions.
    g_RandomBotMinLevel = static_cast<uint8>(sConfigMgr->GetOption<uint32>("AiPlayerbot.RandomBotMinLevel", 1));
    g_RandomBotMaxLevel = static_cast<uint8>(sConfigMgr->GetOption<uint32>("AiPlayerbot.RandomBotMaxLevel", 80));

    // Load the custom number of brackets.
    g_NumRanges = static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.NumRanges", 9));
    g_AllianceLevelRanges.resize(g_NumRanges);
    g_HordeLevelRanges.resize(g_NumRanges);

    // Load Alliance configuration.
    for (uint8 i = 0; i < g_NumRanges; ++i)
    {
        std::string idx = std::to_string(i + 1);
        g_AllianceLevelRanges[i].lower = static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range" + idx + ".Lower", (i == 0 ? 1 : i * 10)));
        g_AllianceLevelRanges[i].upper = static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range" + idx + ".Upper", (i < g_NumRanges - 1 ? i * 10 + 9 : g_RandomBotMaxLevel)));
        g_AllianceLevelRanges[i].desiredPercent = static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range" + idx + ".Pct", 11));
    }

    // Load Horde configuration.
    for (uint8 i = 0; i < g_NumRanges; ++i)
    {
        std::string idx = std::to_string(i + 1);
        g_HordeLevelRanges[i].lower = static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range" + idx + ".Lower", (i == 0 ? 1 : i * 10)));
        g_HordeLevelRanges[i].upper = static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range" + idx + ".Upper", (i < g_NumRanges - 1 ? i * 10 + 9 : g_RandomBotMaxLevel)));
        g_HordeLevelRanges[i].desiredPercent = static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range" + idx + ".Pct", 11));
    }

    // If SyncFactions is enabled, ensure bracket definitions match exactly for both factions.
    // Any mismatch results in immediate server shutdown with an error message.
    if (g_SyncFactions) {
        for (uint8 i = 0; i < g_NumRanges; ++i) {
            if (g_AllianceLevelRanges[i].lower != g_HordeLevelRanges[i].lower ||
                g_AllianceLevelRanges[i].upper != g_HordeLevelRanges[i].upper) {
                LOG_ERROR("server.loading", "[BotLevelBrackets] FATAL: Bracket mismatch detected between factions at index {}. "
                    "Alliance: {}-{}, Horde: {}-{}. "
                    "When SyncFactions is enabled, both bracket number and min/max levels must match exactly. "
                    "Check your configuration.", 
                    i, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper, 
                    g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper);
                std::terminate();
            }
        }
    }

    ClampAndBalanceBrackets();
}


// -----------------------------------------------------------------------------
// BOT DETECTION HELPERS
// -----------------------------------------------------------------------------
/**
 * @brief Checks if the given player is a bot.
 *
 * This function checks if the provided Player pointer is valid and if it has an associated PlayerbotAI instance
 * that indicates it is a bot. It returns true if the player is a bot, false otherwise.
 *
 * @param player Pointer to the Player object to check.
 * @return true if the player is a bot, false otherwise.
 */
static bool IsPlayerBot(Player* player)
{
    if (!player)
    {
        return false;
    }
    PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(player);
    return botAI && botAI->IsBotAI();
}


/**
 * @brief Checks if the given player is a random bot.
 *
 * This function verifies whether the provided Player pointer refers to a random bot
 * managed by the RandomPlayerbotMgr. If the player pointer is null, it returns false.
 *
 * @param player Pointer to the Player object to check.
 * @return true if the player is a random bot, false otherwise.
 */
static bool IsPlayerRandomBot(Player* player)
{
    if (!player)
    {
        return false;
    }
    return sRandomPlayerbotMgr->IsRandomBot(player);
}


/**
 * @brief Checks if the given Player bot belongs to the Alliance team.
 *
 * This function verifies that the provided Player pointer is valid and
 * determines whether the bot's team ID corresponds to the Alliance faction.
 *
 * @param bot Pointer to the Player object representing the bot.
 * @return true if the bot is a member of the Alliance team, false otherwise.
 */
static bool IsAlliancePlayerBot(Player* bot)
{
    return bot && (bot->GetTeamId() == TEAM_ALLIANCE);
}


/**
 * @brief Checks if the given Player bot belongs to the Horde team.
 *
 * This function verifies that the provided Player pointer is valid and
 * determines whether the bot's team ID corresponds to the Horde faction.
 *
 * @param bot Pointer to the Player object representing the bot.
 * @return true if the bot is a member of the Horde team, false otherwise.
 */
static bool IsHordePlayerBot(Player* bot)
{
    return bot && (bot->GetTeamId() == TEAM_HORDE);
}


/**
 * @brief Removes a bot from the list of pending level resets.
 *
 * This function searches the global g_PendingLevelResets container for any entries
 * that match the provided bot's GUID and removes them. It is used to ensure that
 * a bot is no longer scheduled for a pending level reset.
 *
 * @param bot Pointer to the Player object representing the bot to remove.
 */
static void RemoveBotFromPendingResets(Player* bot)
{
    ObjectGuid guid = bot->GetGUID();
    g_PendingLevelResets.erase(
        std::remove_if(
            g_PendingLevelResets.begin(),
            g_PendingLevelResets.end(),
            [guid](const PendingResetEntry& entry) { return entry.botGuid == guid; }
        ),
        g_PendingLevelResets.end()
    );
}


/**
 * @brief Loads the list of social friend GUIDs from the database into the global g_SocialFriendsList.
 *
 * This function clears the existing g_SocialFriendsList and queries the CharacterDatabase
 * for all GUIDs marked as friends (flags = 1) in the character_social table. Each retrieved
 * GUID is added to the g_SocialFriendsList vector. If full debug mode is enabled, the function
 * logs the loading process and each GUID added.
 *
 * The function returns immediately if the query fails or if no results are found.
 */
static void LoadSocialFriendList()
{
    g_SocialFriendsList.clear();
    QueryResult result = CharacterDatabase.Query("SELECT friend FROM character_social WHERE flags = 1");

    if (!result)
    {
        return;
    }
    if (result->GetRowCount() == 0)
    {
        return;
    }
    if (g_BotDistFullDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Fetching Social Friend List GUIDs into array");
    }

    do
    {
        uint32 socialFriendGUID = result->Fetch()->Get<uint32>();
        g_SocialFriendsList.push_back(static_cast<uint64>(socialFriendGUID));
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.load", "[BotLevelBrackets] Adding GUID {} to Social Friend List", socialFriendGUID);
        }
    } while (result->NextRow());
}


/**
 * @brief Loads the persistent guild tracker data from the database.
 *
 * This function queries the bot_level_brackets_guild_tracker table to load all guild IDs
 * that have real players. This provides persistent storage of guild status even when
 * real players are offline. The data is loaded into g_PersistentRealPlayerGuildIds.
 */
static void LoadPersistentGuildTracker()
{
    g_PersistentRealPlayerGuildIds.clear();
    QueryResult result = CharacterDatabase.Query("SELECT guild_id FROM bot_level_brackets_guild_tracker WHERE has_real_players = 1");
    
    if (!result)
    {
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] No guilds with real players found in persistent storage.");
        }
        return;
    }
    
    if (g_BotDistFullDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Loading persistent guild tracker data from database...");
    }
    
    do
    {
        uint32 guildId = result->Fetch()->Get<uint32>();
        g_PersistentRealPlayerGuildIds.insert(guildId);
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Loaded guild {} as having real players.", guildId);
        }
    } while (result->NextRow());
    
    if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Loaded {} guilds with real players from persistent storage.", g_PersistentRealPlayerGuildIds.size());
    }
}


/**
 * @brief Updates the persistent guild tracker database with current guild status.
 *
 * This function adds guilds to the tracker when real players are found online in them.
 * It never removes guilds from the tracker when players log off - this prevents bot level
 * changes from occurring when real players go offline but are still members of the guild.
 */
static void UpdatePersistentGuildTracker()
{
    if (g_BotDistFullDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Starting additive-only persistent guild tracker update...");
    }
    
    // Find guilds with currently online real players
    std::unordered_set<uint32> currentRealPlayerGuilds;
    
    const auto& allPlayers = ObjectAccessor::GetPlayers();
    for (const auto& itr : allPlayers)
    {
        Player* player = itr.second;
        if (!player || !player->IsInWorld())
            continue;
            
        if (!IsPlayerBot(player))
        {
            uint32 guildId = player->GetGuildId();
            if (guildId != 0)
            {
                currentRealPlayerGuilds.insert(guildId);
            }
        }
    }
    
    uint32 addedCount = 0;
    
    // Update or insert guilds with real players - ensure has_real_players is set to 1
    for (uint32 guildId : currentRealPlayerGuilds)
    {
        // Use REPLACE INTO to update existing records or insert new ones
        CharacterDatabase.Execute(
            "REPLACE INTO bot_level_brackets_guild_tracker (guild_id, has_real_players) "
            "VALUES ({}, 1)",
            guildId
        );
        
        // Add to our in-memory cache
        g_PersistentRealPlayerGuildIds.insert(guildId);
        addedCount++;
    }
    
    if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Additive guild tracker update complete. {} guilds processed, {} total tracked guilds.", 
                 addedCount, g_PersistentRealPlayerGuildIds.size());
    }
}


/**
 * @brief Checks and removes guilds from tracker that no longer have any real players online.
 *
 * This function scans all guilds currently in the tracker and removes any that don't have
 * real players online. This is useful for cleaning up after players leave guilds.
 * Should be called manually or as needed, not automatically on logout.
 */
static void CleanupGuildTracker()
{
    if (g_BotDistFullDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Starting guild tracker cleanup - removing guilds with no online real players...");
    }
    
    // Get current guilds with online real players
    std::unordered_set<uint32> currentRealPlayerGuilds;
    const auto& allPlayers = ObjectAccessor::GetPlayers();
    for (const auto& itr : allPlayers)
    {
        Player* player = itr.second;
        if (!player || !player->IsInWorld())
            continue;
            
        if (!IsPlayerBot(player))
        {
            uint32 guildId = player->GetGuildId();
            if (guildId != 0)
            {
                currentRealPlayerGuilds.insert(guildId);
            }
        }
    }
    
    // Find guilds to remove (those in tracker but not in current real player guilds)
    std::vector<uint32> guildsToRemove;
    for (uint32 trackedGuildId : g_PersistentRealPlayerGuildIds)
    {
        if (currentRealPlayerGuilds.find(trackedGuildId) == currentRealPlayerGuilds.end())
        {
            guildsToRemove.push_back(trackedGuildId);
        }
    }
    
    // Remove guilds that no longer have real players online
    uint32 removedCount = 0;
    for (uint32 guildId : guildsToRemove)
    {
        // Remove from database
        CharacterDatabase.Execute(
            "UPDATE bot_level_brackets_guild_tracker SET has_real_players = 0 WHERE guild_id = {}",
            guildId
        );
        
        // Remove from in-memory caches
        g_PersistentRealPlayerGuildIds.erase(guildId);
        g_RealPlayerGuildIds.erase(guildId);
        removedCount++;
        
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Removed guild {} from tracker - no real players online.", guildId);
        }
    }
    
    if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Guild tracker cleanup complete. {} guilds removed, {} guilds remain.", 
                 removedCount, g_PersistentRealPlayerGuildIds.size());
    }
}


/**
 * @brief Populates the global set of real player guild IDs from the provided player map.
 *
 * Iterates through the given map of players, and for each player that is not a bot and is currently in the world,
 * retrieves their guild ID. If the guild ID is non-zero, it is added to the global set of real player guild IDs.
 * The global set is cleared before population.
 *
 * @param players An unordered map associating ObjectGuids with Player pointers to process.
 */
static void LoadRealPlayerGuildIds(const std::unordered_map<ObjectGuid, Player*>& players)
{
    g_RealPlayerGuildIds.clear();
    for (const auto& itr : players)
    {
        Player* player = itr.second;
        if (!player || !player->IsInWorld())
        {
            continue;
        }
        if (!IsPlayerBot(player))
        {
            uint32 guildId = player->GetGuildId();
            if (guildId != 0)
            {
                g_RealPlayerGuildIds.insert(guildId);
            }
        }
    }
}


/**
 * @brief Returns the index of the level range that contains the specified level for the given team.
 *
 * This function checks if the provided level is within the allowed random bot level range.
 * It then searches through the level ranges specific to the team (Alliance or Horde) and returns
 * the index of the range that contains the given level. If the level is not within any range or
 * the team ID is invalid, it returns -1.
 *
 * @param level The level to check.
 * @param teamID The team identifier (TEAM_ALLIANCE or TEAM_HORDE).
 * @return int The index of the matching level range, or -1 if not found or out of bounds.
 */
static int GetLevelRangeIndex(uint8 level, uint8 teamID)
{
    if (level < g_RandomBotMinLevel || level > g_RandomBotMaxLevel)
    {
        return -1;
    }

    if (teamID == TEAM_ALLIANCE)
    {
        for (uint8 i = 0; i < g_NumRanges; ++i)
        {
            if (level >= g_AllianceLevelRanges[i].lower && level <= g_AllianceLevelRanges[i].upper)
            {
                return i;
            }
        }
    }
    else if (teamID == TEAM_HORDE)
    {
        for (uint8 i = 0; i < g_NumRanges; ++i)
        {
            if (level >= g_HordeLevelRanges[i].lower && level <= g_HordeLevelRanges[i].upper)
            {
                return i;
            }
        }
    }

    return -1;
}


/**
 * @brief Returns a random level within the specified range.
 *
 * This function generates a random unsigned 8-bit integer (uint8) between the lower and upper bounds
 * (inclusive) defined in the provided LevelRangeConfig structure.
 *
 * @param range The LevelRangeConfig structure containing the lower and upper bounds for the level range.
 * @return uint8 A random level within the specified range [range.lower, range.upper].
 */
static uint8 GetRandomLevelInRange(const LevelRangeConfig& range)
{
    return urand(range.lower, range.upper);
}


/**
 * @brief Adjusts the level of a player bot to fit within a specified level range bracket.
 *
 * This function ensures that the given bot is valid, in the world, and not in the process of logging out or being removed.
 * It then checks if the target range index is valid and, if the bot is mounted, dismounts it.
 * For Death Knight bots, it enforces a minimum level of 55, skipping adjustment if the target range is below this threshold.
 * The bot's level is then randomized within the specified range, and the bot is re-randomized using PlayerbotFactory.
 * Debug information is logged if enabled, and a system message is sent to the bot to notify about the level reset.
 *
 * @param bot Pointer to the Player object representing the bot to adjust.
 * @param targetRangeIndex Index of the target level range in the factionRanges array.
 * @param factionRanges Pointer to an array of LevelRangeConfig structures defining level brackets for the bot's faction.
 */
static void AdjustBotToRange(Player* bot, int targetRangeIndex, const LevelRangeConfig* factionRanges)
{
    if (!bot || !bot->IsInWorld() || !bot->GetSession() || bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
    {
        return;
    }
    
    if (targetRangeIndex < 0 || targetRangeIndex >= g_NumRanges)
    {
        return;
    }

    if (bot->IsMounted())
    {
        bot->Dismount();
    }

    uint8 botOriginalLevel = bot->GetLevel();
    uint8 newLevel = 0;

    // For Death Knight bots, enforce a minimum level of 55.
    if (bot->getClass() == CLASS_DEATH_KNIGHT)
    {
        uint8 lowerBound = factionRanges[targetRangeIndex].lower;
        uint8 upperBound = factionRanges[targetRangeIndex].upper;
        if (upperBound < 55)
        {
            if (g_BotDistFullDebugMode)
            {
                std::string playerFaction = IsAlliancePlayerBot(bot) ? "Alliance" : "Horde";
                LOG_INFO("server.loading",
                         "[BotLevelBrackets] AdjustBotToRange: Cannot assign {} Death Knight '{}' ({}) to range {}-{} (below level 55).",
                         playerFaction, bot->GetName(), botOriginalLevel, lowerBound, upperBound);
            }
            return;
        }
        if (lowerBound < 55)
        {
            lowerBound = 55;
        }
        if (lowerBound > upperBound)
        {
            return;
        }
        newLevel = urand(lowerBound, upperBound);
    }
    else
    {
        const LevelRangeConfig& range = factionRanges[targetRangeIndex];
        if (range.lower > range.upper)
        {
            if (g_BotDistFullDebugMode)
            {
                std::string playerFaction = IsAlliancePlayerBot(bot) ? "Alliance" : "Horde";
                LOG_INFO("server.loading",
                         "[BotLevelBrackets] AdjustBotToRange: Invalid range {}-{} for {} bot '{}'.",
                         range.lower, range.upper, playerFaction, bot->GetName());
            }
            return;
        }
        newLevel = GetRandomLevelInRange(range);
    }

    PlayerbotFactory newFactory(bot, newLevel);
    newFactory.Randomize(false);

    // Force reset talents if equipment persistence is enabled and bot rolled to max level
    // This is to fix an issue with Playerbots and how Randomization works with Equipment Persistence
    if (newLevel == g_RandomBotMaxLevel && sPlayerbotAIConfig->equipmentPersistence)
    {
        PlayerbotFactory tempFactory(bot, newLevel);
        tempFactory.InitTalentsTree(false, true, true);
    }

    if (g_BotDistFullDebugMode)
    {
        PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(bot);
        std::string playerClassName = botAI ? botAI->GetChatHelper()->FormatClass(bot->getClass()) : "Unknown";
        std::string playerFaction = IsAlliancePlayerBot(bot) ? "Alliance" : "Horde";
        LOG_INFO("server.loading",
                 "[BotLevelBrackets] AdjustBotToRange: {} Bot '{}' - {} ({}) adjusted to level {} (target range {}-{}).",
                 playerFaction, bot->GetName(), playerClassName.c_str(), botOriginalLevel, newLevel,
                 factionRanges[targetRangeIndex].lower, factionRanges[targetRangeIndex].upper);
    }

    ChatHandler(bot->GetSession()).SendSysMessage("[mod-bot-level-brackets] Your level has been reset.");
}


/**
 * @brief Checks if the given bot is in a guild that contains at least one real player.
 *
 * This function verifies several conditions to ensure the bot is valid and currently in the world,
 * has an active session, is not logging out, and is not in the process of being removed from the world.
 * It then checks if the bot is part of a guild and whether that guild is known to contain real players.
 *
 * @param bot Pointer to the Player object representing the bot.
 * @return true if the bot is in a guild with at least one real player, false otherwise.
 */
static bool BotInGuildWithRealPlayer(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->GetSession() || bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
    {
        return false;
    }
    uint32 guildId = bot->GetGuildId();
    if (guildId == 0)
    {
        return false;
    }
    // Check both online real players and persistent database storage
    return g_RealPlayerGuildIds.count(guildId) > 0 || g_PersistentRealPlayerGuildIds.count(guildId) > 0;
}


/**
 * @brief Checks if the given bot is present in any real player's friends list.
 *
 * This function verifies if the provided bot player is valid, currently in the world,
 * has an active session, is not logging out, and is not being removed from the world.
 * It then iterates through the global friends list to determine if the bot's GUID
 * matches any entry, indicating that the bot is on a real player's friends list.
 * If debug mode is enabled, it logs additional information when a match is found.
 *
 * @param bot Pointer to the Player object representing the bot to check.
 * @return true if the bot is on a real player's friends list, false otherwise.
 */
static bool BotInFriendList(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->GetSession() || bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
    {
        return false;
    }

    for (size_t i = 0; i < g_SocialFriendsList.size(); ++i)
    {
        if (g_SocialFriendsList[i] == bot->GetGUID().GetRawValue())
        {
            if (g_BotDistFullDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) is on a Real Player's friends list", bot->GetName(), bot->GetLevel());
            }
            return true;
        }
    }
    return false;
}


/**
 * @brief Checks if the given bot is a member of any arena team.
 *
 * This function verifies that the provided Player pointer is valid and
 * checks all arena team slots to see if the bot is in any arena team.
 *
 * @param bot Pointer to the Player object representing the bot.
 * @return true if the bot is in an arena team, false otherwise.
 */
static bool BotInArenaTeam(Player* bot)
{
    if (!bot)
        return false;
    for (uint32 slot = 0; slot < MAX_ARENA_SLOT; ++slot)
    {
        if (ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(bot->GetArenaTeamId(slot)))
        {
            return true;
        }
    }
    return false;
}


/**
 * @brief Clamps and balances the level brackets for Alliance and Horde bot distributions.
 *
 * This function ensures that the lower and upper bounds of each level bracket for both Alliance and Horde
 * are within the allowed minimum and maximum bot levels. If a bracket's lower bound exceeds its upper bound,
 * its desired percentage is set to zero. After clamping, the function checks if the sum of desired percentages
 * for each faction equals 100. If not, and the total is greater than zero, it auto-adjusts the percentages
 * upwards (one at a time, round-robin) until the sum reaches 100. Debug information is logged if enabled.
 *
 * Globals used:
 * - g_AllianceLevelRanges: Array of Alliance level brackets.
 * - g_HordeLevelRanges: Array of Horde level brackets.
 * - g_NumRanges: Number of level brackets.
 * - g_RandomBotMinLevel: Minimum allowed bot level.
 * - g_RandomBotMaxLevel: Maximum allowed bot level.
 * - g_BotDistFullDebugMode: Debug mode flag.
 */
static void ClampAndBalanceBrackets()
{
    for (uint8 i = 0; i < g_NumRanges; ++i)
    {
        if (g_AllianceLevelRanges[i].lower < g_RandomBotMinLevel)
        {
            g_AllianceLevelRanges[i].lower = g_RandomBotMinLevel;
        }
        if (g_AllianceLevelRanges[i].upper > g_RandomBotMaxLevel)
        {
            g_AllianceLevelRanges[i].upper = g_RandomBotMaxLevel;
        }
        if (g_AllianceLevelRanges[i].lower > g_AllianceLevelRanges[i].upper)
        {
            g_AllianceLevelRanges[i].desiredPercent = 0;
        }
    }
    for (uint8 i = 0; i < g_NumRanges; ++i)
    {
        if (g_HordeLevelRanges[i].lower < g_RandomBotMinLevel)
        {
            g_HordeLevelRanges[i].lower = g_RandomBotMinLevel;
        }
        if (g_HordeLevelRanges[i].upper > g_RandomBotMaxLevel)
        {
            g_HordeLevelRanges[i].upper = g_RandomBotMaxLevel;
        }
        if (g_HordeLevelRanges[i].lower > g_HordeLevelRanges[i].upper)
        {
            g_HordeLevelRanges[i].desiredPercent = 0;
        }
    }
    uint32 totalAlliance = 0;
    uint32 totalHorde = 0;
    for (uint8 i = 0; i < g_NumRanges; ++i)
    {
        totalAlliance += g_AllianceLevelRanges[i].desiredPercent;
        totalHorde += g_HordeLevelRanges[i].desiredPercent;
    }
    if (totalAlliance != 100 && totalAlliance > 0)
    {
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Alliance: Sum of percentages is {} (expected 100). Auto adjusting.", totalAlliance);
        }
        int missing = 100 - totalAlliance;
        while (missing > 0)
        {
            for (uint8 i = 0; i < g_NumRanges && missing > 0; ++i)
            {
                if (g_AllianceLevelRanges[i].lower <= g_AllianceLevelRanges[i].upper && g_AllianceLevelRanges[i].desiredPercent > 0)
                {
                    g_AllianceLevelRanges[i].desiredPercent++;
                    missing--;
                }
            }
        }
    }
    if (totalHorde != 100 && totalHorde > 0)
    {
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Horde: Sum of percentages is {} (expected 100). Auto adjusting.", totalHorde);
        }
        int missing = 100 - totalHorde;
        while (missing > 0)
        {
            for (uint8 i = 0; i < g_NumRanges && missing > 0; ++i)
            {
                if (g_HordeLevelRanges[i].lower <= g_HordeLevelRanges[i].upper && g_HordeLevelRanges[i].desiredPercent > 0)
                {
                    g_HordeLevelRanges[i].desiredPercent++;
                    missing--;
                }
            }
        }
    }
}


/**
 * @brief Checks if a bot is in a safe state to perform a level reset.
 *
 * This function verifies several conditions to ensure that the provided bot is safe for a level reset operation.
 * The checks include:
 * - The bot pointer and its session are valid and not in the process of logging out or being removed from the world.
 * - The bot is currently in the world and alive.
 * - The bot is not in combat.
 * - The bot is not in a battleground, arena, random dungeon, or battleground queue.
 * - The bot is not in flight.
 * - If the bot is in a group, all group members must also be bots.
 *
 * If any of these conditions are not met, the function returns false. If debugging is enabled via
 * g_BotDistFullDebugMode, detailed log messages are generated for each failure case.
 *
 * @param bot Pointer to the Player object representing the bot.
 * @return true if the bot is safe for level reset, false otherwise.
 */
static bool IsBotSafeForLevelReset(Player* bot)
{
    if (!bot || !bot->GetSession() || bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
    {
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Null bot pointer provided.");
        }
        return false;
    }
    if (!bot->IsInWorld())
    {
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) is not in world.", bot->GetName(), bot->GetLevel());
        }
        return false;
    }
    if (!bot->IsAlive())
    {
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) is not alive.", bot->GetName(), bot->GetLevel());
        }
        return false;
    }
    if (bot->IsInCombat())
    {
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) is in combat.", bot->GetName(), bot->GetLevel());
        }
        return false;
    }
    if (bot->InBattleground() || bot->InArena() || bot->inRandomLfgDungeon() || bot->InBattlegroundQueue())
    {
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) is in battleground, arena, random dungeon, or battleground queue.", bot->GetName(), bot->GetLevel());
        }
        return false;
    }
    if (bot->IsInFlight())
    {
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) is in flight.", bot->GetName(), bot->GetLevel());
        }
        return false;
    }
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member->IsInWorld() && !IsPlayerBot(member))
            {
                if (g_BotDistFullDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Bot {} (Level {}) has non-bot group member {} (Level {}).", bot->GetName(), bot->GetLevel(), member->GetName(), member->GetLevel());
                }
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief Checks if a given bot is in the exclusion list for bracket processing.
 *
 * This function determines whether the provided bot's name matches any entry in the global
 * exclusion list `g_ExcludeBotNames`, which is populated from the BotLevelBrackets.ExcludeNames config.
 * If a match is found, this bot will not be considered for any bracket checks or level resets.
 *
 * @param bot Pointer to the Player object representing the bot to check.
 * @return true if the bot is excluded from bracket processing, false otherwise.
 */
static bool IsBotExcluded(Player* bot)
{
    if (!bot)
    {
        return false;
    }
    const std::string& name = bot->GetName();
    for (const auto& excluded : g_ExcludeBotNames)
    {
        if (excluded == name)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Processes the pending level reset requests for player bots.
 *
 * This function iterates through the global list of pending level resets (`g_PendingLevelResets`)
 * and attempts to reset the level of each eligible bot to a specified range. The function enforces
 * a configurable limit (`g_FlaggedProcessLimit`) on the number of resets processed per cycle.
 *
 * Bots are skipped and removed from the pending list if:
 *   - The bot is not found or not in the world.
 *   - The bot's session is invalid, logging out, or being removed from the world.
 *   - The bot is in a guild with real players and `g_IgnoreGuildBotsWithRealPlayers` is enabled.
 *   - The bot is in a friend list and `g_IgnoreFriendListed` is enabled.
 *
 * If a bot is eligible and safe for a level reset, its level is adjusted to the target range using
 * `AdjustBotToRange`. Debug information is logged if `g_BotDistFullDebugMode` is enabled.
 *
 * The function returns immediately if there are no pending resets.
 */
static void ProcessPendingLevelResets()
{
    if (g_BotDistFullDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Processing {} pending resets...", g_PendingLevelResets.size());
    }
    if (g_PendingLevelResets.empty())
    {
        return;
    }

    // Limit the number of resets processed in one cycle if configured.
    uint32 processed = 0;
    for (auto it = g_PendingLevelResets.begin(); it != g_PendingLevelResets.end(); )
        {
            if (g_FlaggedProcessLimit > 0 && processed >= g_FlaggedProcessLimit)
                break;

            Player* bot = ObjectAccessor::FindPlayer(it->botGuid);
            
            if (!bot)
            {
                it = g_PendingLevelResets.erase(it);
                continue;
            }

            if (!bot->IsInWorld() || !bot->GetSession() || bot->GetSession()->isLogingOut() || bot->IsDuringRemoveFromWorld())
            {
                it = g_PendingLevelResets.erase(it);
                continue;
            }

            if (IsBotExcluded(bot))
            {
                it = g_PendingLevelResets.erase(it);
                continue;
            }

            int targetRange = it->targetRange;
            if (g_IgnoreGuildBotsWithRealPlayers && BotInGuildWithRealPlayer(bot))
            {
                it = g_PendingLevelResets.erase(it);
                continue;
            }

            if (g_IgnoreFriendListed && BotInFriendList(bot))
            {
                it = g_PendingLevelResets.erase(it);
                continue;
            }

            if (g_IgnoreArenaTeamBots && BotInArenaTeam(bot))
            {
                it = g_PendingLevelResets.erase(it);
                continue;
            }

            // Check if bot is now in a group with real players
            if (Group* group = bot->GetGroup())
            {
                bool hasRealPlayer = false;
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                {
                    Player* member = ref->GetSource();
                    if (member && member->IsInWorld() && !IsPlayerBot(member))
                    {
                        hasRealPlayer = true;
                        break;
                    }
                }
                if (hasRealPlayer)
                {
                    it = g_PendingLevelResets.erase(it);
                    continue;
                }
            }

            if (bot && bot->IsInWorld() && IsBotSafeForLevelReset(bot))
            {
                AdjustBotToRange(bot, targetRange, it->factionRanges);
                if (g_BotDistFullDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Bot '{}' successfully reset to level range {}-{}.", bot->GetName(), it->factionRanges[targetRange].lower, it->factionRanges[targetRange].upper);
                }
                it = g_PendingLevelResets.erase(it);
                ++processed;
            }
            else
            {
                ++it;
            }
        }
}


/**
 * @brief Determines the level bracket index for a given player or flags the player for a level reset if not in any bracket.
 *
 * This function attempts to find the appropriate level range (bracket) for the specified player based on their level and faction.
 * If the player is not currently within any defined bracket, the function finds the closest bracket and flags the player for a level reset,
 * ensuring that each player is only flagged once. The function returns the index of the bracket if found, or -1 if the player is not in any bracket.
 *
 * @param player Pointer to the Player object whose bracket is to be determined.
 * @return int The index of the level bracket if found, otherwise -1.
 */
static int GetOrFlagPlayerBracket(Player* player)
{
    if (IsPlayerBot(player) && IsBotExcluded(player))
    {
        return -1;
    }

    if (IsPlayerBot(player) && g_IgnoreGuildBotsWithRealPlayers && BotInGuildWithRealPlayer(player))
    {
        return -1;
    }

    if (IsPlayerBot(player) && g_IgnoreArenaTeamBots && BotInArenaTeam(player))
    {
        return -1;
    }

    // Check if bot is in a group with real players - if so, exclude from bracket processing
    if (IsPlayerBot(player))
    {
        if (Group* group = player->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (member && member->IsInWorld() && !IsPlayerBot(member))
                {
                    if (g_BotDistFullDebugMode)
                    {
                        LOG_INFO("server.loading", "[BotLevelBrackets] GetOrFlagPlayerBracket: Bot {} (Level {}) is in group with real player {} - excluding from bracket processing.", 
                                 player->GetName(), player->GetLevel(), member->GetName());
                    }
                    return -1;
                }
            }
        }
    }

    int rangeIndex = GetLevelRangeIndex(player->GetLevel(), player->GetTeamId());
    if (rangeIndex >= 0)
    {
        return rangeIndex;
    }

    LevelRangeConfig* factionRanges = nullptr;
    if (IsAlliancePlayerBot(player))
    {
        factionRanges = g_AllianceLevelRanges.data();
    }
    else if (IsHordePlayerBot(player))
    {
        factionRanges = g_HordeLevelRanges.data();
    }
    else
    {
        return -1; // Unknown faction
    }

    int targetRange = -1;
    int smallestDiff = std::numeric_limits<int>::max();
    for (int i = 0; i < g_NumRanges; ++i)
    {
        if (factionRanges[i].lower > factionRanges[i].upper)
        {
            continue;
        }
        
        // Skip brackets that Death Knights cannot be assigned to (upper bound < 55)
        if (player->getClass() == CLASS_DEATH_KNIGHT && factionRanges[i].upper < 55)
        {
            continue;
        }
        
        int diff = 0;
        if (player->GetLevel() < factionRanges[i].lower)
        {
            diff = factionRanges[i].lower - player->GetLevel();
        }
        else if (player->GetLevel() > factionRanges[i].upper)
        {
            diff = player->GetLevel() - factionRanges[i].upper;
        }
        if (diff < smallestDiff)
        {
            smallestDiff = diff;
            targetRange = i;
        }
    }

    if (targetRange >= 0)
    {
        bool alreadyFlagged = false;
        ObjectGuid guid = player->GetGUID();
        for (const auto &entry : g_PendingLevelResets)
        {
            if (entry.botGuid == guid)
            {
                alreadyFlagged = true;
                break;
            }
        }
        if (!alreadyFlagged)
        {
            g_PendingLevelResets.push_back({guid, targetRange, factionRanges});
        }
    }

    return -1;
}


// -----------------------------------------------------------------------------
// WORLD SCRIPT: Bot Level Distribution with Faction Separation
// -----------------------------------------------------------------------------
/**
 * @class BotLevelBracketsWorldScript
 * @brief WorldScript implementation for dynamic bot level bracket distribution in AzerothCore.
 *
 * This script manages the distribution of player bots across predefined level brackets for both Alliance and Horde factions.
 * It dynamically adjusts the desired percentage of bots in each bracket based on the real player population, ensuring a balanced
 * and realistic distribution of bots throughout the server. The script also handles pending level resets for bots to move them
 * between brackets as needed.
 *
 * Key Features:
 * - Loads configuration and social data on startup.
 * - Periodically recalculates desired bot distribution based on real player bracket counts.
 * - Supports both synchronized and separate faction weighting modes.
 * - Efficiently redistributes surplus bots to underpopulated brackets, flagging them for level reset.
 * - Skips bots in guilds with real players or on friend lists if configured.
 * - Provides detailed debug logging for all major operations and decisions.
 *
 * Main Methods:
 * - OnStartup(): Loads configuration and logs initial state.
 * - OnUpdate(uint32 diff): Periodically checks and adjusts bot distribution, processes pending level resets.
 *
 * Member Variables:
 * - m_timer: Tracks time for periodic distribution adjustments.
 * - m_flaggedTimer: Tracks time for processing pending level resets.
 */
class BotLevelBracketsWorldScript : public WorldScript
{
public:
    BotLevelBracketsWorldScript() : WorldScript("BotLevelBracketsWorldScript"), m_timer(0), m_flaggedTimer(0), m_guildTrackerTimer(0) { }

    /**
     * @brief Called when the module is started up.
     *
     * This function initializes the Bot Level Brackets module by loading configuration
     * and social friend list data. It checks if the module is enabled via configuration,
     * and logs a message if it is disabled. If debug modes are enabled, it logs detailed
     * information about the module's configuration, including check frequencies and
     * desired percentage distributions for Alliance and Horde level ranges.
     */
    void OnStartup() override
    {
        LoadBotLevelBracketsConfig();
        LoadSocialFriendList();
        LoadPersistentGuildTracker();
        if (!g_BotLevelBracketsEnabled)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Module disabled via configuration.");
            return;
        }
        if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Module loaded. Check frequency: {} seconds, Check flagged frequency: {}.", g_BotDistCheckFrequency, g_BotDistFlaggedCheckFrequency);
            for (uint8 i = 0; i < g_NumRanges; ++i)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Alliance Range {}: {}-{}, Desired Percentage: {}%",
                         i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper, g_AllianceLevelRanges[i].desiredPercent);
            }
            for (uint8 i = 0; i < g_NumRanges; ++i)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Horde Range {}: {}-{}, Desired Percentage: {}%",
                         i + 1, g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper, g_HordeLevelRanges[i].desiredPercent);
            }
        }
    }


    /**
     * @brief Periodically updates the bot level bracket distribution for player bots.
     *
     * This function is called on a timed interval and is responsible for:
     * - Checking if the bot level bracket system is enabled.
     * - Managing timers for regular and flagged bot checks.
     * - Processing pending level resets for bots flagged for redistribution.
     * - Gathering all players and updating guild and friend list caches.
     * - If dynamic distribution is enabled, recalculates the desired percentage of bots per level bracket
     *   based on the current distribution of real players, optionally syncing between factions.
     * - For each faction (Alliance and Horde):
     *   - Counts the actual number of bots in each level bracket.
     *   - Determines the desired number of bots per bracket based on the calculated percentages.
     *   - Identifies surplus bots in overpopulated brackets and flags them for level reset to underpopulated brackets,
     *     prioritizing "safe" bots (those eligible for immediate reset) and then flagged bots.
     *   - Updates internal counts to reflect pending redistributions.
     * - Provides detailed debug logging if enabled, including before and after distributions.
     *
     * The function ensures that the distribution of player bots across level brackets remains balanced
     * according to the current configuration and real player population, improving the gameplay experience.
     *
     * @param diff The time in milliseconds since the last update call.
     */
    void OnUpdate(uint32 diff) override
    {
        if (!g_BotLevelBracketsEnabled)
        {
            return;
        }
        
        m_timer += diff;
        m_flaggedTimer += diff;
        m_guildTrackerTimer += diff;

        if (m_flaggedTimer >= g_BotDistFlaggedCheckFrequency * 1000)
        {
            if (g_BotDistFullDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Pending Level Resets Triggering.");
            }
            ProcessPendingLevelResets();
            m_flaggedTimer = 0;
        }

        if (m_guildTrackerTimer >= g_GuildTrackerUpdateFrequency * 1000)
        {
            if (g_BotDistFullDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Guild Tracker Update Triggering.");
            }
            UpdatePersistentGuildTracker();
            m_guildTrackerTimer = 0;
        }

        if (m_timer < g_BotDistCheckFrequency * 1000)
        {
            return;
        }
        m_timer = 0;

        const auto& allPlayers = ObjectAccessor::GetPlayers();

        LoadRealPlayerGuildIds(allPlayers);

        LoadSocialFriendList();

        if (g_UseDynamicDistribution)
        {
            // Calculate real player bracket counts
            std::vector<int> allianceRealCounts(g_NumRanges, 0);
            std::vector<int> hordeRealCounts(g_NumRanges, 0);
            uint32 totalAllianceReal = 0;
            uint32 totalHordeReal = 0;

            for (auto const& itr : allPlayers)
            {
                Player* player = itr.second;
                if (!player || !player->IsInWorld())
                    continue;
                if (IsPlayerBot(player))
                    continue; // Only count real players.
                int rangeIndex = GetOrFlagPlayerBracket(player);
                if (rangeIndex < 0)
                    continue;
                if (player->GetTeamId() == TEAM_ALLIANCE)
                {
                    allianceRealCounts[rangeIndex]++;
                    totalAllianceReal++;
                }
                else if (player->GetTeamId() == TEAM_HORDE)
                {
                    hordeRealCounts[rangeIndex]++;
                    totalHordeReal++;
                }
            }

            const float baseline = 1.0f;
            std::vector<float> allianceWeights(g_NumRanges, 0.0f);
            std::vector<float> hordeWeights(g_NumRanges, 0.0f);

            // SYNCED MODE: Real player weighting is combined for both factions, applied to both bracket tables.
            if (g_SyncFactions)
            {
                uint32 totalCombinedReal = totalAllianceReal + totalHordeReal;
                for (int i = 0; i < g_NumRanges; ++i)
                {
                    int combinedReal = allianceRealCounts[i] + hordeRealCounts[i];
                    float weight = baseline + g_RealPlayerWeight *
                        (totalCombinedReal > 0 ? (1.0f / float(totalCombinedReal)) : 1.0f) *
                        log(1 + combinedReal);
                    allianceWeights[i] = weight;
                    hordeWeights[i] = weight;
                }
            }
            else
            {
                // Separate dynamic weighting for each faction
                for (int i = 0; i < g_NumRanges; ++i)
                {
                    if (g_AllianceLevelRanges[i].lower > g_AllianceLevelRanges[i].upper)
                        allianceWeights[i] = 0.0f;
                    else
                        allianceWeights[i] = baseline + g_RealPlayerWeight *
                            (totalAllianceReal > 0 ? (1.0f / totalAllianceReal) : 1.0f) *
                            log(1 + allianceRealCounts[i]);

                    if (g_HordeLevelRanges[i].lower > g_HordeLevelRanges[i].upper)
                        hordeWeights[i] = 0.0f;
                    else
                        hordeWeights[i] = baseline + g_RealPlayerWeight *
                            (totalHordeReal > 0 ? (1.0f / totalHordeReal) : 1.0f) *
                            log(1 + hordeRealCounts[i]);
                }
            }

            // Helper for normalizing weights so percentages sum to 100
            auto applyWeights = [](std::vector<LevelRangeConfig>& ranges, const std::vector<float>& weights)
            {
                float total = 0.0f;
                for (int i = 0; i < g_NumRanges; ++i)
                    total += weights[i];
                int pctSum = 0;
                for (int i = 0; i < g_NumRanges; ++i)
                {
                    uint8 pct = (total > 0.0f) ? static_cast<uint8>(round((weights[i] / total) * 100)) : 0;
                    ranges[i].desiredPercent = pct;
                    pctSum += pct;
                }
                // Fix rounding drift so sum = 100
                int missing = 100 - pctSum;
                for (int i = 0; i < g_NumRanges && missing > 0; ++i)
                {
                    if (ranges[i].lower <= ranges[i].upper && ranges[i].desiredPercent > 0)
                    {
                        ranges[i].desiredPercent++;
                        missing--;
                    }
                }
            };

            applyWeights(g_AllianceLevelRanges, allianceWeights);
            applyWeights(g_HordeLevelRanges, hordeWeights);

            // Ensure brackets respect global min/max levels and percentages sum to 100
            ClampAndBalanceBrackets();

            // Debug output for new bracket percentages after normalization
            if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
            {
                for (int i = 0; i < g_NumRanges; ++i)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Final Range {}: {}-{}, Alliance Desired: {}%, Horde Desired: {}%",
                        i + 1,
                        g_AllianceLevelRanges[i].lower,
                        g_AllianceLevelRanges[i].upper,
                        g_AllianceLevelRanges[i].desiredPercent,
                        g_HordeLevelRanges[i].desiredPercent);
                }
            }
        }
        
        uint32 totalAllianceBots = 0;
        std::vector<int> allianceActualCounts(g_NumRanges, 0);
        std::vector< std::vector<Player*> > allianceBotsByRange(g_NumRanges);

        uint32 totalHordeBots = 0;
        std::vector<int> hordeActualCounts(g_NumRanges, 0);
        std::vector< std::vector<Player*> > hordeBotsByRange(g_NumRanges);

        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Starting processing of {} players.", allPlayers.size());
        }

        for (auto const& itr : allPlayers)
        {
            Player* player = itr.second;
            if (!player)
            {
                if (g_BotDistFullDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Skipping null player.");
                }
                continue;
            }
            if (!player->IsInWorld())
            {
                if (g_BotDistFullDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Skipping player '{}' as they are not in world.", player->GetName());
                }
                continue;
            }
            if (!IsPlayerBot(player) || !IsPlayerRandomBot(player))
            {
                if (g_BotDistFullDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Skipping player '{}' as they are not a random bot.", player->GetName());
                }
                continue;
            }
            if (IsBotExcluded(player))
            {
                if (g_BotDistFullDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Skipping excluded bot '{}'.", player->GetName());
                }
                continue;
            }
            if (g_IgnoreGuildBotsWithRealPlayers && BotInGuildWithRealPlayer(player))
            {
                continue;
            }
            if (g_IgnoreFriendListed && BotInFriendList(player))
            {
                continue;
            }
            if (g_IgnoreArenaTeamBots && BotInArenaTeam(player))
            {
                continue;
            }
            if (IsAlliancePlayerBot(player))
            {
                totalAllianceBots++;
                int rangeIndex = GetOrFlagPlayerBracket(player);
                if (rangeIndex >= 0)
                {
                    allianceActualCounts[rangeIndex]++;
                    allianceBotsByRange[rangeIndex].push_back(player);
                    if (g_BotDistFullDebugMode)
                    {
                        LOG_INFO("server.loading", "[BotLevelBrackets] Alliance bot '{}' with level {} added to range {}.", 
                                 player->GetName(), player->GetLevel(), rangeIndex + 1);
                    }
                }
                else if (g_BotDistFullDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Alliance bot '{}' with level {} does not fall into any defined range.", player->GetName(), player->GetLevel());
                }
            }
            else if (IsHordePlayerBot(player))
            {
                totalHordeBots++;
                int rangeIndex = GetOrFlagPlayerBracket(player);
                if (rangeIndex >= 0)
                {
                    hordeActualCounts[rangeIndex]++;
                    hordeBotsByRange[rangeIndex].push_back(player);
                    if (g_BotDistFullDebugMode)
                    {
                        LOG_INFO("server.loading", "[BotLevelBrackets] Horde bot '{}' with level {} added to range {}.", 
                                 player->GetName(), player->GetLevel(), rangeIndex + 1);
                    }
                }
                else if (g_BotDistFullDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Horde bot '{}' with level {} does not fall into any defined range.", player->GetName(), player->GetLevel());
                }
            }
        }

        if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] =========================================");
            LOG_INFO("server.loading", "[BotLevelBrackets] Total Alliance Bots: {}.", totalAllianceBots);
            LOG_INFO("server.loading", "[BotLevelBrackets] Total Horde Bots: {}.", totalHordeBots);
            LOG_INFO("server.loading", "[BotLevelBrackets] =========================================");
        }

        // Process Alliance bots.
        if (totalAllianceBots > 0)
        {
            if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] =========================================");
            }
            std::vector<int> allianceDesiredCounts(g_NumRanges, 0);
            for (int i = 0; i < g_NumRanges; ++i)
            {
                allianceDesiredCounts[i] = static_cast<int>(round((g_AllianceLevelRanges[i].desiredPercent / 100.0) * totalAllianceBots));
                if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Alliance Range {} ({}-{}): Desired = {}, Actual = {}.", 
                            i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper,
                            allianceDesiredCounts[i], allianceActualCounts[i]);
                }
            }

            for (int i = 0; i < g_NumRanges; ++i)
            {
                // Collect safe and flagged bots
                std::vector<Player*> safeBots;
                std::vector<Player*> flaggedBots;
                for (Player* bot : allianceBotsByRange[i])
                {
                    if (IsBotSafeForLevelReset(bot)) {
                        safeBots.push_back(bot);
                    } else {
                        flaggedBots.push_back(bot);
                    }
                }

                // --------- Efficient surplus redistribution for safeBots ----------
                // Build a list of target ranges that need bots
                std::vector<int> targetRanges;
                for (int j = 0; j < g_NumRanges; ++j)
                {
                    if (allianceActualCounts[j] < allianceDesiredCounts[j])
                        targetRanges.push_back(j);
                }
                size_t targetIdx = 0;
                while (allianceActualCounts[i] > allianceDesiredCounts[i] && !safeBots.empty() && targetIdx < targetRanges.size())
                {
                    Player* bot = safeBots.back();
                    safeBots.pop_back();

                    int targetRange = targetRanges[targetIdx];

                    // Skip if no need (already filled by earlier loop)
                    if (allianceActualCounts[targetRange] >= allianceDesiredCounts[targetRange])
                    {
                        targetIdx++;
                        continue;
                    }

                    // Only flag if not already flagged
                    ObjectGuid botGuid = bot->GetGUID();
                    bool alreadyFlagged = false;
                    for (const auto& entry : g_PendingLevelResets)
                    {
                        if (entry.botGuid == botGuid)
                        {
                            alreadyFlagged = true;
                            break;
                        }
                    }
                    if (!alreadyFlagged)
                    {
                        g_PendingLevelResets.push_back({bot->GetGUID(), targetRange, g_AllianceLevelRanges.data()});
                        if (g_BotDistFullDebugMode)
                        {
                            LOG_INFO("server.loading", "[BotLevelBrackets] Alliance bot '{}' flagged for pending level reset to range {}-{}.", 
                                bot->GetName(), g_AllianceLevelRanges[targetRange].lower, g_AllianceLevelRanges[targetRange].upper);
                        }
                    }
                    allianceActualCounts[i]--;
                    allianceActualCounts[targetRange]++;
                    if (allianceActualCounts[targetRange] >= allianceDesiredCounts[targetRange])
                        targetIdx++;
                }
                // --------- Efficient surplus redistribution for flaggedBots ----------
                // Reset for flagged bots
                targetIdx = 0;
                while (allianceActualCounts[i] > allianceDesiredCounts[i] && !flaggedBots.empty() && targetIdx < targetRanges.size())
                {
                    Player* bot = flaggedBots.back();
                    flaggedBots.pop_back();

                    int targetRange = targetRanges[targetIdx];

                    if (allianceActualCounts[targetRange] >= allianceDesiredCounts[targetRange])
                    {
                        targetIdx++;
                        continue;
                    }

                    ObjectGuid botGuid = bot->GetGUID();
                    bool alreadyFlagged = false;
                    for (const auto& entry : g_PendingLevelResets)
                    {
                        if (entry.botGuid == botGuid)
                        {
                            alreadyFlagged = true;
                            break;
                        }
                    }
                    if (!alreadyFlagged)
                    {
                        g_PendingLevelResets.push_back({bot->GetGUID(), targetRange, g_AllianceLevelRanges.data()});
                        if (g_BotDistFullDebugMode)
                        {
                            LOG_INFO("server.loading", "[BotLevelBrackets] Alliance flagged bot '{}' flagged for pending level reset to range {}-{}.", 
                                bot->GetName(), g_AllianceLevelRanges[targetRange].lower, g_AllianceLevelRanges[targetRange].upper);
                        }
                    }
                    allianceActualCounts[i]--;
                    allianceActualCounts[targetRange]++;
                    if (allianceActualCounts[targetRange] >= allianceDesiredCounts[targetRange])
                        targetIdx++;
                }
            }
        }


        // Process Horde bots.
        if (totalHordeBots > 0)
        {
            if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] =========================================");
            }
            std::vector<int> hordeDesiredCounts(g_NumRanges, 0);
            for (int i = 0; i < g_NumRanges; ++i)
            {
                hordeDesiredCounts[i] = static_cast<int>(round((g_HordeLevelRanges[i].desiredPercent / 100.0) * totalHordeBots));
                if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Horde Range {} ({}-{}): Desired = {}, Actual = {}.",
                            i + 1, g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper,
                            hordeDesiredCounts[i], hordeActualCounts[i]);
                }
            }

            for (int i = 0; i < g_NumRanges; ++i)
            {
                std::vector<Player*> safeBots;
                std::vector<Player*> flaggedBots;
                for (Player* bot : hordeBotsByRange[i])
                {
                    if (IsBotSafeForLevelReset(bot)) {
                        safeBots.push_back(bot);
                    } else {
                        flaggedBots.push_back(bot);
                    }
                }

                // Efficient surplus redistribution for safeBots
                std::vector<int> targetRanges;
                for (int j = 0; j < g_NumRanges; ++j)
                {
                    if (hordeActualCounts[j] < hordeDesiredCounts[j])
                        targetRanges.push_back(j);
                }
                size_t targetIdx = 0;
                while (hordeActualCounts[i] > hordeDesiredCounts[i] && !safeBots.empty() && targetIdx < targetRanges.size())
                {
                    Player* bot = safeBots.back();
                    safeBots.pop_back();

                    int targetRange = targetRanges[targetIdx];

                    if (hordeActualCounts[targetRange] >= hordeDesiredCounts[targetRange])
                    {
                        targetIdx++;
                        continue;
                    }

                    bool alreadyFlagged = false;
                    ObjectGuid botGuid = bot->GetGUID();
                    for (const auto& entry : g_PendingLevelResets)
                    {
                        if (entry.botGuid == botGuid)
                        {
                            alreadyFlagged = true;
                            break;
                        }
                    }
                    if (!alreadyFlagged)
                    {
                        g_PendingLevelResets.push_back({bot->GetGUID(), targetRange, g_HordeLevelRanges.data()});
                        if (g_BotDistFullDebugMode)
                        {
                            LOG_INFO("server.loading", "[BotLevelBrackets] Horde bot '{}' flagged for pending level reset to range {}-{}.", 
                                bot->GetName(), g_HordeLevelRanges[targetRange].lower, g_HordeLevelRanges[targetRange].upper);
                        }
                    }
                    hordeActualCounts[i]--;
                    hordeActualCounts[targetRange]++;
                    if (hordeActualCounts[targetRange] >= hordeDesiredCounts[targetRange])
                        targetIdx++;
                }
                // Efficient surplus redistribution for flaggedBots
                targetIdx = 0;
                while (hordeActualCounts[i] > hordeDesiredCounts[i] && !flaggedBots.empty() && targetIdx < targetRanges.size())
                {
                    Player* bot = flaggedBots.back();
                    flaggedBots.pop_back();

                    int targetRange = targetRanges[targetIdx];

                    if (hordeActualCounts[targetRange] >= hordeDesiredCounts[targetRange])
                    {
                        targetIdx++;
                        continue;
                    }

                    bool alreadyFlagged = false;
                    ObjectGuid botGuid = bot->GetGUID();
                    for (const auto& entry : g_PendingLevelResets)
                    {
                        if (entry.botGuid == botGuid)
                        {
                            alreadyFlagged = true;
                            break;
                        }
                    }
                    if (!alreadyFlagged)
                    {
                        g_PendingLevelResets.push_back({bot->GetGUID(), targetRange, g_HordeLevelRanges.data()});
                        if (g_BotDistFullDebugMode)
                        {
                            LOG_INFO("server.loading", "[BotLevelBrackets] Horde flagged bot '{}' flagged for pending level reset to range {}-{}.", 
                                bot->GetName(), g_HordeLevelRanges[targetRange].lower, g_HordeLevelRanges[targetRange].upper);
                        }
                    }
                    hordeActualCounts[i]--;
                    hordeActualCounts[targetRange]++;
                    if (hordeActualCounts[targetRange] >= hordeDesiredCounts[targetRange])
                        targetIdx++;
                }
            }
        }


        if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] ========================================= COMPLETE");
            LOG_INFO("server.loading", "[BotLevelBrackets] Distribution adjustment complete. Alliance bots: {}, Horde bots: {}.",
                     totalAllianceBots, totalHordeBots);
            LOG_INFO("server.loading", "[BotLevelBrackets] =========================================");
            std::vector<int> allianceDesiredCounts(g_NumRanges, 0);
            for (int i = 0; i < g_NumRanges; ++i)
            {
                allianceDesiredCounts[i] = static_cast<int>(round((g_AllianceLevelRanges[i].desiredPercent / 100.0) * totalAllianceBots));
                LOG_INFO("server.loading", "[BotLevelBrackets] Alliance Range {} ({}-{}): Desired = {}, Actual = {}.", 
                         i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper,
                         allianceDesiredCounts[i], allianceActualCounts[i]);
            }
            LOG_INFO("server.loading", "[BotLevelBrackets] ----------------------------------------");
            std::vector<int> hordeDesiredCounts(g_NumRanges, 0);
            for (int i = 0; i < g_NumRanges; ++i)
            {
                hordeDesiredCounts[i] = static_cast<int>(round((g_HordeLevelRanges[i].desiredPercent / 100.0) * totalHordeBots));
                LOG_INFO("server.loading", "[BotLevelBrackets] Horde Range {} ({}-{}): Desired = {}, Actual = {}.",
                         i + 1, g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper,
                         hordeDesiredCounts[i], hordeActualCounts[i]);
            }
            LOG_INFO("server.loading", "[BotLevelBrackets] =========================================");
        }
    }

    /**
     * @brief Manually trigger guild tracker cleanup.
     *
     * This function can be called to remove guilds from the tracker that no longer have
     * real players online. This is useful after players leave guilds to ensure accurate
     * tracking and allow bot level changes in guilds that truly have no real players.
     *
     * Call this periodically or when you know players have left guilds to clean up the tracker.
     */
    void ManualGuildTrackerCleanup()
    {
        if (!g_BotLevelBracketsEnabled || !g_IgnoreGuildBotsWithRealPlayers)
        {
            return;
        }
        
        CleanupGuildTracker();
    }

private:
    uint32 m_timer;         // For distribution adjustments
    uint32 m_flaggedTimer;  // For pending reset checks
    uint32 m_guildTrackerTimer; // For guild tracker updates
};


/**
 * @class BotLevelBracketsPlayerScript
 * @brief Handles player-specific logic for the Player Bot Level Brackets module.
 *
 * This script is attached to player events and is responsible for managing
 * bot-related state when a player logs out. Specifically, it ensures that
 * any bot associated with the player is removed from pending reset operations.
 *
 * @see PlayerScript
 */
class BotLevelBracketsPlayerScript : public PlayerScript
{
public:
    BotLevelBracketsPlayerScript() : PlayerScript("BotLevelBracketsPlayerScript") {}

    void OnPlayerLogout(Player* player)
    {
        RemoveBotFromPendingResets(player);
    }
};

/**
 * @class BotLevelBracketsCommandScript
 * @brief Handles chat commands for the Player Bot Level Brackets module.
 *
 * This script provides administrative commands to manage the bot level brackets configuration.
 */
class BotLevelBracketsCommandScript : public CommandScript
{
public:
    BotLevelBracketsCommandScript() : CommandScript("BotLevelBracketsCommandScript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "reload", HandleReloadConfig, SEC_ADMINISTRATOR, Console::No }
        };
        return commandTable;
    }

    static bool HandleReloadConfig(ChatHandler* handler)
    {
        LoadBotLevelBracketsConfig();
        handler->SendSysMessage("Bot level brackets config reloaded.");
        return true;
    }
};

// -----------------------------------------------------------------------------
// ENTRY POINT: Register the Bot Level Distribution Module
// -----------------------------------------------------------------------------
/**
 * @brief Registers the world, player, and command scripts for the Player Bot Level Brackets module.
 *
 * This function instantiates and adds the BotLevelBracketsWorldScript, BotLevelBracketsPlayerScript,
 * and BotLevelBracketsCommandScript to the script system, enabling custom logic and commands
 * for player bot level brackets within the game world.
 */
void Addmod_player_bot_level_bracketsScripts()
{
    new BotLevelBracketsWorldScript();
    new BotLevelBracketsPlayerScript();
    new BotLevelBracketsCommandScript();
}
