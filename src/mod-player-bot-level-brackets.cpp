#include "ScriptMgr.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "Chat.h"
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
#include <unordered_map>

// Forward declarations.
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

// New configuration option to enable/disable the mod. Default is true.
static bool g_BotLevelBracketsEnabled = true;
// New configuration option to ignore bots in guilds with a real player online. Default is true.
static bool g_IgnoreGuildBotsWithRealPlayers = true;

// Use vectors to store the level ranges.
static std::vector<LevelRangeConfig> g_AllianceLevelRanges;
static std::vector<LevelRangeConfig> g_HordeLevelRanges;

static uint32 g_BotDistCheckFrequency = 300; // in seconds
static uint32 g_BotDistFlaggedCheckFrequency = 15; // in seconds
static bool   g_BotDistFullDebugMode      = false;
static bool   g_BotDistLiteDebugMode      = false;
static bool   g_UseDynamicDistribution  = false;
static bool   g_IgnoreFriendListed = true;

// Real player weight to boost bracket contributions.
static float g_RealPlayerWeight = 1.0f;

// Array for character social list friends
std::vector<int> SocialFriendsList;

// Persistent guild tracking variables
static std::unordered_map<uint32, bool> g_GuildHasRealPlayer;
static uint32 g_GuildTrackingUpdateFrequency = 600; // 10 minutes in seconds

// -----------------------------------------------------------------------------
// Loads the configuration from the config file.
// -----------------------------------------------------------------------------
static void LoadBotLevelBracketsConfig()
{
    g_BotLevelBracketsEnabled = sConfigMgr->GetOption<bool>("BotLevelBrackets.Enabled", true);
    g_IgnoreGuildBotsWithRealPlayers = sConfigMgr->GetOption<bool>("BotLevelBrackets.IgnoreGuildBotsWithRealPlayers", true);
    
    g_BotDistFullDebugMode = sConfigMgr->GetOption<bool>("BotLevelBrackets.FullDebugMode", false);
    g_BotDistLiteDebugMode = sConfigMgr->GetOption<bool>("BotLevelBrackets.LiteDebugMode", false);
    g_BotDistCheckFrequency = sConfigMgr->GetOption<uint32>("BotLevelBrackets.CheckFrequency", 300);
    g_BotDistFlaggedCheckFrequency = sConfigMgr->GetOption<uint32>("BotLevelBrackets.CheckFlaggedFrequency", 15);
    g_GuildTrackingUpdateFrequency = sConfigMgr->GetOption<uint32>("BotLevelBrackets.GuildTrackingUpdateFrequency", 600);
    g_UseDynamicDistribution = sConfigMgr->GetOption<bool>("BotLevelBrackets.UseDynamicDistribution", false);
    g_RealPlayerWeight = sConfigMgr->GetOption<float>("BotLevelBrackets.RealPlayerWeight", 1.0f);
    g_IgnoreFriendListed = sConfigMgr->GetOption<bool>("BotLevelBrackets.IgnoreFriendListed", true);

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

    ClampAndBalanceBrackets();
}

// -----------------------------------------------------------------------------
// Loads the friend guid(s) from character_social into array
// -----------------------------------------------------------------------------
static void LoadSocialFriendList()
{
    SocialFriendsList.clear();
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
        SocialFriendsList.push_back(socialFriendGUID);
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.load", "[BotLevelBrackets] Adding GUID {} to Social Friend List", socialFriendGUID);
        }
    } while (result->NextRow());
}

// -----------------------------------------------------------------------------
// Loads the guild tracking data from the database into memory
// -----------------------------------------------------------------------------
static void LoadGuildTrackingData()
{
    g_GuildHasRealPlayer.clear();
    QueryResult result = WorldDatabase.Query("SELECT guild_id, has_real_player FROM mod_bot_level_brackets_guild_tracker");

    if (!result)
    {
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] No guild tracking data found in database");
        }
        return;
    }

    if (g_BotDistFullDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Loading guild tracking data from database");
    }

    do
    {
        uint32 guildId = result->Fetch()[0].Get<uint32>();
        bool hasRealPlayer = result->Fetch()[1].Get<bool>();
        g_GuildHasRealPlayer[guildId] = hasRealPlayer;
        
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Loaded guild {} with real player status: {}", guildId, hasRealPlayer);
        }
    } while (result->NextRow());
    
    if (g_BotDistFullDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Loaded {} guild tracking entries", g_GuildHasRealPlayer.size());
    }
}

// -----------------------------------------------------------------------------
// Updates the guild tracking table with current guild membership data
// -----------------------------------------------------------------------------
static void UpdateGuildTrackingData()
{
    if (g_BotDistFullDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Starting guild tracking data update");
    }

    // Track which guilds we've seen in the current scan
    std::unordered_map<uint32, bool> currentGuildStatus;

    // Scan all characters in the database to determine current guild membership
    QueryResult result = CharacterDatabase.Query(
        "SELECT c.guid, c.name, g.guildid "
        "FROM characters c "
        "LEFT JOIN guild_member g ON c.guid = g.guid "
        "WHERE c.deleteDate IS NULL AND g.guildid IS NOT NULL"
    );

    if (result)
    {
        std::unordered_map<uint32, std::vector<uint32>> guildMembers;
        
        // Group characters by guild
        do
        {
            uint32 charGuid = result->Fetch()[0].Get<uint32>();
            uint32 guildId = result->Fetch()[2].Get<uint32>();
            guildMembers[guildId].push_back(charGuid);
        } while (result->NextRow());

        // For each guild, check if it has any real (non-bot) players
        for (auto& [guildId, members] : guildMembers)
        {
            bool hasRealPlayer = false;
            
            for (uint32 charGuid : members)
            {
                // Check if this character is a bot by looking in playerbots table
                QueryResult botCheck = CharacterDatabase.Query("SELECT 1 FROM playerbots WHERE playerbot = {}", charGuid);
                if (!botCheck)
                {
                    // Character is not in playerbots table, so it's a real player
                    hasRealPlayer = true;
                    break;
                }
            }
            
            currentGuildStatus[guildId] = hasRealPlayer;
            
            if (g_BotDistFullDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Guild {} has {} members, real player: {}", 
                         guildId, members.size(), hasRealPlayer);
            }
        }
    }

    // Update database with changes
    for (auto& [guildId, hasRealPlayer] : currentGuildStatus)
    {
        // Check if this guild status has changed or is new
        auto it = g_GuildHasRealPlayer.find(guildId);
        if (it == g_GuildHasRealPlayer.end() || it->second != hasRealPlayer)
        {
            // Insert or update the guild status
            WorldDatabase.Execute(
                "INSERT INTO mod_bot_level_brackets_guild_tracker (guild_id, has_real_player) "
                "VALUES ({}, {}) "
                "ON DUPLICATE KEY UPDATE has_real_player = {}, last_updated = CURRENT_TIMESTAMP",
                guildId, hasRealPlayer, hasRealPlayer
            );
            
            if (g_BotDistFullDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Updated guild {} real player status to {}", 
                         guildId, hasRealPlayer);
            }
        }
        
        // Update in-memory cache
        g_GuildHasRealPlayer[guildId] = hasRealPlayer;
    }

    // Remove guilds that no longer exist
    for (auto it = g_GuildHasRealPlayer.begin(); it != g_GuildHasRealPlayer.end();)
    {
        if (currentGuildStatus.find(it->first) == currentGuildStatus.end())
        {
            // Guild no longer exists, remove from database and memory
            WorldDatabase.Execute("DELETE FROM mod_bot_level_brackets_guild_tracker WHERE guild_id = {}", it->first);
            
            if (g_BotDistFullDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Removed non-existent guild {} from tracking", it->first);
            }
            
            it = g_GuildHasRealPlayer.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (g_BotDistFullDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Guild tracking data update completed. Tracking {} guilds", 
                 g_GuildHasRealPlayer.size());
    }
}

// Returns the index of the level bracket that the given level belongs to.
// If the bot is out of range, it returns -1
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

// Returns a random level within the provided range.
static uint8 GetRandomLevelInRange(const LevelRangeConfig& range)
{
    return urand(range.lower, range.upper);
}

// Adjusts a bot's level by selecting a random level within the target range.
static void AdjustBotToRange(Player* bot, int targetRangeIndex, const LevelRangeConfig* factionRanges)
{
    if (!bot || targetRangeIndex < 0 || targetRangeIndex >= g_NumRanges)
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
        newLevel = urand(lowerBound, upperBound);
    }
    else
    {
        newLevel = GetRandomLevelInRange(factionRanges[targetRangeIndex]);
    }

    PlayerbotFactory newFactory(bot, newLevel);
    newFactory.Randomize(false);

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

// -----------------------------------------------------------------------------
// BOT DETECTION HELPERS
// -----------------------------------------------------------------------------
static bool IsPlayerBot(Player* player)
{
    if (!player)
    {
        return false;
    }
    PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(player);
    return botAI && botAI->IsBotAI();
}

static bool IsPlayerRandomBot(Player* player)
{
    if (!player)
    {
        return false;
    }
    return sRandomPlayerbotMgr->IsRandomBot(player);
}

static bool IsAlliancePlayerBot(Player* bot)
{
    return bot && (bot->GetTeamId() == TEAM_ALLIANCE);
}

static bool IsHordePlayerBot(Player* bot)
{
    return bot && (bot->GetTeamId() == TEAM_HORDE);
}

static void LogAllBotLevels()
{
    if (g_BotDistFullDebugMode)
    {
        std::map<uint8, uint32> botLevelCount;
        for (auto const& itr : ObjectAccessor::GetPlayers())
        {
            Player* player = itr.second;
            if (!player || !player->IsInWorld())
            {
                continue;
            }
            if (!IsPlayerBot(player))
            {
                continue;
            }
            botLevelCount[player->GetLevel()]++;
        }
        for (const auto& entry : botLevelCount)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Level {}: {} bots", entry.first, entry.second);
        }
    }
}

// -----------------------------------------------------------------------------
// HELPER FUNCTION: Check if a bot is in a guild with at least one real player (persistent check).
// -----------------------------------------------------------------------------
static bool BotInGuildWithRealPlayer(Player* bot)
{
    if (!bot)
    {
        return false;
    }
    uint32 guildId = bot->GetGuildId();
    if (guildId == 0)
    {
        return false;
    }

    // Use the persistent guild tracking data instead of checking online players
    auto it = g_GuildHasRealPlayer.find(guildId);
    if (it != g_GuildHasRealPlayer.end())
    {
        return it->second;
    }

    // If guild not found in cache, assume no real players for safety
    // This should not happen if the system is working correctly
    if (g_BotDistFullDebugMode)
    {
        LOG_INFO("server.loading", "[BotLevelBrackets] Guild {} not found in tracking cache, assuming no real players", guildId);
    }
    return false;
}

static bool BotInFriendList(Player* bot)
{
    if (!bot)
    {
        return false;
    }

    for (size_t i = 0; i < SocialFriendsList.size(); ++i)
    {
        if (g_BotDistFullDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Check bot {} against SocialFriendsList Array Character GUID {}", bot->GetName(), SocialFriendsList[i]);
        }
        if (SocialFriendsList[i] == bot->GetGUID().GetRawValue())
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

// -----------------------------------------------------------------------------
// SAFETY CHECKS FOR LEVEL RESET
// -----------------------------------------------------------------------------
static bool IsBotSafeForLevelReset(Player* bot)
{
    if (!bot)
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
            if (member && !IsPlayerBot(member))
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

// Global container to hold bots flagged for pending level reset.
// Each entry is a pair: the bot and the target level range index along with a pointer to the faction config.
struct PendingResetEntry
{
    Player* bot;
    int targetRange;
    const LevelRangeConfig* factionRanges;
};
static std::vector<PendingResetEntry> g_PendingLevelResets;

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

    for (auto it = g_PendingLevelResets.begin(); it != g_PendingLevelResets.end(); )
    {
        Player* bot = it->bot;
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
        if (bot && bot->IsInWorld() && IsBotSafeForLevelReset(bot))
        {
            AdjustBotToRange(bot, targetRange, it->factionRanges);
            if (g_BotDistFullDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Bot '{}' successfully reset to level range {}-{}.", bot->GetName(), it->factionRanges[targetRange].lower, it->factionRanges[targetRange].upper);
            }
            it = g_PendingLevelResets.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

// This function returns a valid bracket index for the given player's level.
// If the player's level is out of range (GetLevelRangeIndex returns -1),
// it computes the closest valid bracket using the provided factionRanges,
// flags the player for a pending reset by adding it to g_PendingLevelResets,
static int GetOrFlagPlayerBracket(Player* player)
{
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
        for (const auto &entry : g_PendingLevelResets)
        {
            if (entry.bot == player)
            {
                alreadyFlagged = true;
                break;
            }
        }
        if (!alreadyFlagged)
        {
            g_PendingLevelResets.push_back({player, targetRange, factionRanges});
        }
    }

    return -1;
}

// -----------------------------------------------------------------------------
// WORLD SCRIPT: Bot Level Distribution with Faction Separation
// -----------------------------------------------------------------------------
class BotLevelBracketsWorldScript : public WorldScript
{
public:
    BotLevelBracketsWorldScript() : WorldScript("BotLevelBracketsWorldScript"), m_timer(0), m_flaggedTimer(0), m_guildTrackingTimer(0) { }

    void OnStartup() override
    {
        LoadBotLevelBracketsConfig();
        LoadSocialFriendList();
        LoadGuildTrackingData();
        if (!g_BotLevelBracketsEnabled)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Module disabled via configuration.");
            return;
        }
        if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Module loaded. Check frequency: {} seconds, Check flagged frequency: {}, Guild tracking update frequency: {} seconds.", g_BotDistCheckFrequency, g_BotDistFlaggedCheckFrequency, g_GuildTrackingUpdateFrequency);
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

    void OnUpdate(uint32 diff) override
    {
        if (!g_BotLevelBracketsEnabled)
        {
            return;
        }
        
        m_timer += diff;
        m_flaggedTimer += diff;
        m_guildTrackingTimer += diff;

        if (m_flaggedTimer >= g_BotDistFlaggedCheckFrequency * 1000)
        {
            if (g_BotDistFullDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Pending Level Resets Triggering.");
            }
            ProcessPendingLevelResets();
            m_flaggedTimer = 0;
        }

        // Update guild tracking data every 10 minutes
        if (m_guildTrackingTimer >= g_GuildTrackingUpdateFrequency * 1000)
        {
            if (g_BotDistFullDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Guild tracking update triggering.");
            }
            UpdateGuildTrackingData();
            m_guildTrackingTimer = 0;
        }

        if (m_timer < g_BotDistCheckFrequency * 1000)
        {
            return;
        }
        m_timer = 0;

        LoadSocialFriendList();

        if (g_UseDynamicDistribution)
        {
            std::vector<int> allianceRealCounts(g_NumRanges, 0);
            std::vector<int> hordeRealCounts(g_NumRanges, 0);
            uint32 totalAllianceReal = 0;
            uint32 totalHordeReal = 0;
            for (auto const& itr : ObjectAccessor::GetPlayers())
            {
                Player* player = itr.second;
                if (!player || !player->IsInWorld())
                {
                    continue;
                }
                if (IsPlayerBot(player))
                {
                    continue; // Skip bots.
                }
                int rangeIndex = GetOrFlagPlayerBracket(player);
                if (rangeIndex < 0)
                {
                    continue;
                }
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
            float allianceTotalWeight = 0.0f;
            float hordeTotalWeight = 0.0f;
            for (int i = 0; i < g_NumRanges; ++i)
            {
                if (g_AllianceLevelRanges[i].lower > g_AllianceLevelRanges[i].upper)
                {
                    allianceWeights[i] = 0.0f;
                }
                else
                {
                    allianceWeights[i] = baseline + g_RealPlayerWeight *
                        (totalAllianceReal > 0 ? (1.0f / totalAllianceReal) : 1.0f) *
                        log(1 + allianceRealCounts[i]);
                }

                if (g_HordeLevelRanges[i].lower > g_HordeLevelRanges[i].upper)
                {
                    hordeWeights[i] = 0.0f;
                }
                else
                {
                    hordeWeights[i] = baseline + g_RealPlayerWeight *
                        (totalHordeReal > 0 ? (1.0f / totalHordeReal) : 1.0f) *
                        log(1 + hordeRealCounts[i]);
                }

                allianceTotalWeight += allianceWeights[i];
                hordeTotalWeight += hordeWeights[i];
            }
            for (int i = 0; i < g_NumRanges; ++i)
            {
                g_AllianceLevelRanges[i].desiredPercent = static_cast<uint8>(round((allianceWeights[i] / allianceTotalWeight) * 100));
                if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Dynamic Distribution - Alliance Range {}: {}-{}, Real Players: {} (weight: {:.2f}), New Desired: {}%",
                             i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper, allianceRealCounts[i], allianceWeights[i], g_AllianceLevelRanges[i].desiredPercent);
                }
            }

            uint8 sumAlliance = 0;
            for (int i = 0; i < g_NumRanges; ++i)
            {
                sumAlliance += g_AllianceLevelRanges[i].desiredPercent;
            }
            if (sumAlliance < 100 && allianceTotalWeight > 0)
            {
                uint8 missing = 100 - sumAlliance;
                if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Alliance normalization: current sum = {}, missing = {}", sumAlliance, missing);
                }
                while (missing > 0)
                {
                    for (int i = 0; i < g_NumRanges && missing > 0; ++i)
                    {
                        if (g_AllianceLevelRanges[i].lower <= g_AllianceLevelRanges[i].upper && allianceWeights[i] > 0)
                        {
                            g_AllianceLevelRanges[i].desiredPercent++;
                            missing--;
                        }
                    }
                }
                if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Alliance normalized percentages:");
                    for (int i = 0; i < g_NumRanges; ++i)
                    {
                        LOG_INFO("server.loading", "    Range {}: {}% ({}-{})", i + 1, g_AllianceLevelRanges[i].desiredPercent,
                                 g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper);
                    }
                }
            }
            
            for (int i = 0; i < g_NumRanges; ++i)
            {
                g_HordeLevelRanges[i].desiredPercent = static_cast<uint8>(round((hordeWeights[i] / hordeTotalWeight) * 100));
                if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Dynamic Distribution - Horde Range {}: {}-{}, Real Players: {} (weight: {:.2f}), New Desired: {}%",
                             i + 1, g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper, hordeRealCounts[i], hordeWeights[i], g_HordeLevelRanges[i].desiredPercent);
                }
            }

            uint8 sumHorde = 0;
            for (int i = 0; i < g_NumRanges; ++i)
            {
                sumHorde += g_HordeLevelRanges[i].desiredPercent;
            }
            if (sumHorde < 100 && hordeTotalWeight > 0)
            {
                uint8 missing = 100 - sumHorde;
                if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Horde normalization: current sum = {}, missing = {}", sumHorde, missing);
                }
                while (missing > 0)
                {
                    for (int i = 0; i < g_NumRanges && missing > 0; ++i)
                    {
                        if (g_HordeLevelRanges[i].lower <= g_HordeLevelRanges[i].upper && hordeWeights[i] > 0)
                        {
                            g_HordeLevelRanges[i].desiredPercent++;
                            missing--;
                        }
                    }
                }
                if (g_BotDistFullDebugMode || g_BotDistLiteDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Horde normalized percentages:");
                    for (int i = 0; i < g_NumRanges; ++i)
                    {
                        LOG_INFO("server.loading", "    Range {}: {}% ({}-{})", i + 1, g_HordeLevelRanges[i].desiredPercent,
                                 g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper);
                    }
                }
            }
        }

        uint32 totalAllianceBots = 0;
        std::vector<int> allianceActualCounts(g_NumRanges, 0);
        std::vector< std::vector<Player*> > allianceBotsByRange(g_NumRanges);

        uint32 totalHordeBots = 0;
        std::vector<int> hordeActualCounts(g_NumRanges, 0);
        std::vector< std::vector<Player*> > hordeBotsByRange(g_NumRanges);

        auto const& allPlayers = ObjectAccessor::GetPlayers();
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
            if (g_IgnoreGuildBotsWithRealPlayers && BotInGuildWithRealPlayer(player))
            {
                continue;
            }
            if (g_IgnoreFriendListed && BotInFriendList(player))
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
                if (g_BotDistFullDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] >>> Processing Alliance bots in range {}.", i + 1);
                }
                std::vector<Player*> safeBots;
                std::vector<Player*> flaggedBots;
                for (Player* bot : allianceBotsByRange[i])
                {
                    if (IsBotSafeForLevelReset(bot))
                    {
                        safeBots.push_back(bot);
                    }
                    else
                    {
                        flaggedBots.push_back(bot);
                        if (g_BotDistFullDebugMode)
                        {
                            LOG_INFO("server.loading", "[BotLevelBrackets] Alliance bot '{}' is NOT safe for level reset in range {}.", 
                                     bot->GetName(), i + 1);
                        }
                    }
                }
                while (allianceActualCounts[i] > allianceDesiredCounts[i] && !safeBots.empty())
                {
                    Player* bot = safeBots.back();
                    safeBots.pop_back();
                    if (g_BotDistFullDebugMode)
                    {
                        LOG_INFO("server.loading", "[BotLevelBrackets] Alliance safe bot '{}' from range {} will be moved.", bot->GetName(), i + 1);
                    }
                    int targetRange = -1;
                    if (bot->getClass() == CLASS_DEATH_KNIGHT)
                    {
                        for (int j = 0; j < g_NumRanges; ++j)
                        {
                            if (allianceActualCounts[j] < allianceDesiredCounts[j] && g_AllianceLevelRanges[j].upper >= 55)
                            {
                                targetRange = j;
                                break;
                            }
                        }
                    }
                    else
                    {
                        for (int j = 0; j < g_NumRanges; ++j)
                        {
                            if (allianceActualCounts[j] < allianceDesiredCounts[j])
                            {
                                targetRange = j;
                                break;
                            }
                        }
                    }
                    if (targetRange == -1)
                    {
                        if (g_BotDistFullDebugMode)
                        {
                            LOG_INFO("server.loading", "[BotLevelBrackets] No valid target range found for alliance safe bot '{}'.", bot->GetName());
                        }
                        break;
                    }
                    if (g_BotDistFullDebugMode)
                    {
                        LOG_INFO("server.loading", "[BotLevelBrackets] !!!! Adjusting alliance bot '{}' from range {} to range {} ({}-{}).",  
                                 bot->GetName(), i + 1, targetRange + 1, g_AllianceLevelRanges[targetRange].lower, g_AllianceLevelRanges[targetRange].upper);
                    }
                    AdjustBotToRange(bot, targetRange, g_AllianceLevelRanges.data());
                    allianceActualCounts[i]--;
                    allianceActualCounts[targetRange]++;
                }
                while (allianceActualCounts[i] > allianceDesiredCounts[i] && !flaggedBots.empty())
                {
                    Player* bot = flaggedBots.back();
                    flaggedBots.pop_back();
                    if (g_BotDistFullDebugMode)
                    {
                        LOG_INFO("server.loading", "[BotLevelBrackets] Alliance flagged bot '{}' from range {} will be processed for pending reset.", bot->GetName(), i + 1);
                    }
                    int targetRange = -1;
                    if (bot->getClass() == CLASS_DEATH_KNIGHT)
                    {
                        for (int j = 0; j < g_NumRanges; ++j)
                        {
                            if (allianceActualCounts[j] < allianceDesiredCounts[j] && g_AllianceLevelRanges[j].upper >= 55)
                            {
                                targetRange = j;
                                break;
                            }
                        }
                    }
                    else
                    {
                        for (int j = 0; j < g_NumRanges; ++j)
                        {
                            if (allianceActualCounts[j] < allianceDesiredCounts[j])
                            {
                                targetRange = j;
                                break;
                            }
                        }
                    }
                    if (targetRange == -1)
                    {
                        if (g_BotDistFullDebugMode)
                        {
                            LOG_INFO("server.loading", "[BotLevelBrackets] No valid target range found for flagged alliance bot '{}' for pending reset.", bot->GetName());
                        }
                        break;
                    }
                    bool alreadyFlagged = false;
                    for (auto& entry : g_PendingLevelResets)
                    {
                        if (entry.bot == bot)
                        {
                            alreadyFlagged = true;
                            break;
                        }
                    }
                    if (!alreadyFlagged)
                    {
                        g_PendingLevelResets.push_back({bot, targetRange, g_AllianceLevelRanges.data()});
                        if (g_BotDistFullDebugMode)
                        {
                            LOG_INFO("server.loading", "[BotLevelBrackets] Alliance bot '{}' flagged for pending level reset to range {}-{}.", 
                                     bot->GetName(), g_AllianceLevelRanges[targetRange].lower, g_AllianceLevelRanges[targetRange].upper);
                        }
                    }
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
                if (g_BotDistFullDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Processing Horde bots in range {}.", i + 1);
                }
                std::vector<Player*> safeBots;
                std::vector<Player*> flaggedBots;
                for (Player* bot : hordeBotsByRange[i])
                {
                    if (IsBotSafeForLevelReset(bot))
                    {
                        safeBots.push_back(bot);
                    }
                    else
                    {
                        flaggedBots.push_back(bot);
                        if (g_BotDistFullDebugMode)
                        {
                            LOG_INFO("server.loading", "[BotLevelBrackets] Horde bot '{}' is NOT safe for level reset in range {}.", 
                                     bot->GetName(), i + 1);
                        }
                    }
                }
                while (hordeActualCounts[i] > hordeDesiredCounts[i] && !safeBots.empty())
                {
                    Player* bot = safeBots.back();
                    safeBots.pop_back();
                    if (g_BotDistFullDebugMode)
                    {
                        LOG_INFO("server.loading", "[BotLevelBrackets] Horde safe bot '{}' from range {} will be moved.", bot->GetName(), i + 1);
                    }
                    int targetRange = -1;
                    if (bot->getClass() == CLASS_DEATH_KNIGHT)
                    {
                        for (int j = 0; j < g_NumRanges; ++j)
                        {
                            if (hordeActualCounts[j] < hordeDesiredCounts[j] && g_HordeLevelRanges[j].upper >= 55)
                            {
                                targetRange = j;
                                break;
                            }
                        }
                    }
                    else
                    {
                        for (int j = 0; j < g_NumRanges; ++j)
                        {
                            if (hordeActualCounts[j] < hordeDesiredCounts[j])
                            {
                                targetRange = j;
                                break;
                            }
                        }
                    }
                    if (targetRange == -1)
                    {
                        if (g_BotDistFullDebugMode)
                        {
                            LOG_INFO("server.loading", "[BotLevelBrackets] No valid target range found for safe horde bot '{}'.", bot->GetName());
                        }
                        break;
                    }
                    if (g_BotDistFullDebugMode)
                    {
                        LOG_INFO("server.loading", "[BotLevelBrackets] !!!! Adjusting horde bot '{}' from range {} to range {} ({}-{}).", 
                                 bot->GetName(), i + 1, targetRange + 1, g_HordeLevelRanges[targetRange].lower, g_HordeLevelRanges[targetRange].upper);
                    }
                    AdjustBotToRange(bot, targetRange, g_HordeLevelRanges.data());
                    hordeActualCounts[i]--;
                    hordeActualCounts[targetRange]++;
                }
                while (hordeActualCounts[i] > hordeDesiredCounts[i] && !flaggedBots.empty())
                {
                    Player* bot = flaggedBots.back();
                    flaggedBots.pop_back();
                    if (g_BotDistFullDebugMode)
                    {
                        LOG_INFO("server.loading", "[BotLevelBrackets] Horde flagged bot '{}' from range {} will be processed for pending reset.", bot->GetName(), i + 1);
                    }
                    int targetRange = -1;
                    if (bot->getClass() == CLASS_DEATH_KNIGHT)
                    {
                        for (int j = 0; j < g_NumRanges; ++j)
                        {
                            if (hordeActualCounts[j] < hordeDesiredCounts[j] && g_HordeLevelRanges[j].upper >= 55)
                            {
                                targetRange = j;
                                break;
                            }
                        }
                    }
                    else
                    {
                        for (int j = 0; j < g_NumRanges; ++j)
                        {
                            if (hordeActualCounts[j] < hordeDesiredCounts[j])
                            {
                                targetRange = j;
                                break;
                            }
                        }
                    }
                    if (targetRange == -1)
                    {
                        if (g_BotDistFullDebugMode)
                        {
                            LOG_INFO("server.loading", "[BotLevelBrackets] No valid target range found for flagged horde bot '{}' for pending reset.", bot->GetName());
                        }
                        break;
                    }
                    bool alreadyFlagged = false;
                    for (auto& entry : g_PendingLevelResets)
                    {
                        if (entry.bot == bot)
                        {
                            alreadyFlagged = true;
                            break;
                        }
                    }
                    if (!alreadyFlagged)
                    {
                        g_PendingLevelResets.push_back({bot, targetRange, g_HordeLevelRanges.data()});
                        if (g_BotDistFullDebugMode)
                        {
                            LOG_INFO("server.loading", "[BotLevelBrackets] Horde bot '{}' flagged for pending level reset to range {}-{}.", 
                                     bot->GetName(), g_HordeLevelRanges[targetRange].lower, g_HordeLevelRanges[targetRange].upper);
                        }
                    }
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

private:
    uint32 m_timer;                // For distribution adjustments
    uint32 m_flaggedTimer;         // For pending reset checks
    uint32 m_guildTrackingTimer;   // For guild tracking updates
};

// -----------------------------------------------------------------------------
// ENTRY POINT: Register the Bot Level Distribution Module
// -----------------------------------------------------------------------------
void Addmod_player_bot_level_bracketsScripts()
{
    new BotLevelBracketsWorldScript();
}
