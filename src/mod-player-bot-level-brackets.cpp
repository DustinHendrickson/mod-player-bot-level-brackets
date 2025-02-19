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
#include "PlayerbotFactory.h"

void RemoveAllEquippedItems(Player* bot);
void RemoveAllTradeSkills(Player* bot);
void RemoveAllQuests(Player* bot);

static bool IsAlliancePlayerBot(Player* bot);
static bool IsHordePlayerBot(Player* bot);

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

static const uint8 NUM_RANGES = 9;

// Separate arrays for Alliance and Horde.
static LevelRangeConfig g_AllianceLevelRanges[NUM_RANGES];
static LevelRangeConfig g_HordeLevelRanges[NUM_RANGES];

static uint32 g_BotDistCheckFrequency = 300; // in seconds
static uint32 g_BotDistFlaggedCheckFrequency = 15; // in seconds
static bool   g_BotDistDebugMode      = false;

// Loads the configuration from the config file.
static void LoadBotLevelBracketsConfig()
{
    g_BotDistDebugMode = sConfigMgr->GetOption<bool>("BotLevelBrackets.DebugMode", false);
    g_BotDistCheckFrequency = sConfigMgr->GetOption<uint32>("BotLevelBrackets.CheckFrequency", 300);
    g_BotDistFlaggedCheckFrequency = sConfigMgr->GetOption<uint32>("BotLevelBrackets.CheckFlaggedFrequency", 15);

    // Alliance configuration.
    g_AllianceLevelRanges[0] = { 1, 9,   static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range1Pct", 12)) };
    g_AllianceLevelRanges[1] = { 10, 19, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range2Pct", 11)) };
    g_AllianceLevelRanges[2] = { 20, 29, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range3Pct", 11)) };
    g_AllianceLevelRanges[3] = { 30, 39, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range4Pct", 11)) };
    g_AllianceLevelRanges[4] = { 40, 49, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range5Pct", 11)) };
    g_AllianceLevelRanges[5] = { 50, 59, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range6Pct", 11)) };
    g_AllianceLevelRanges[6] = { 60, 69, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range7Pct", 11)) };
    g_AllianceLevelRanges[7] = { 70, 79, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range8Pct", 11)) };
    g_AllianceLevelRanges[8] = { 80, 80, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Alliance.Range9Pct", 11)) };

    // Horde configuration.
    g_HordeLevelRanges[0] = { 1, 9,   static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range1Pct", 12)) };
    g_HordeLevelRanges[1] = { 10, 19, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range2Pct", 11)) };
    g_HordeLevelRanges[2] = { 20, 29, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range3Pct", 11)) };
    g_HordeLevelRanges[3] = { 30, 39, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range4Pct", 11)) };
    g_HordeLevelRanges[4] = { 40, 49, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range5Pct", 11)) };
    g_HordeLevelRanges[5] = { 50, 59, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range6Pct", 11)) };
    g_HordeLevelRanges[6] = { 60, 69, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range7Pct", 11)) };
    g_HordeLevelRanges[7] = { 70, 79, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range8Pct", 11)) };
    g_HordeLevelRanges[8] = { 80, 80, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotLevelBrackets.Horde.Range9Pct", 11)) };

    uint32 totalAlliancePercent = 0;
    uint32 totalHordePercent = 0;
    for (uint8 i = 0; i < NUM_RANGES; ++i)
    {
        totalAlliancePercent += g_AllianceLevelRanges[i].desiredPercent;
        totalHordePercent += g_HordeLevelRanges[i].desiredPercent;
    }
    if (totalAlliancePercent != 100)
        LOG_ERROR("server.loading", "[BotLevelBrackets] Alliance: Sum of percentages is {} (expected 100).", totalAlliancePercent);
    if (totalHordePercent != 100)
        LOG_ERROR("server.loading", "[BotLevelBrackets] Horde: Sum of percentages is {} (expected 100).", totalHordePercent);
}

// Returns the index of the level range that the given level belongs to (boundaries are the same for both factions).
static int GetLevelRangeIndex(uint8 level)
{
    // Here we assume that all ranges share the same lower and upper bounds.
    // (If not, you may need to adjust this based on faction.)
    for (int i = 0; i < NUM_RANGES; ++i)
    {
        // Use Alliance boundaries as reference.
        if (level >= g_AllianceLevelRanges[i].lower && level <= g_AllianceLevelRanges[i].upper)
            return i;
    }
    return -1;
}

// Returns a random level within the provided range.
static uint8 GetRandomLevelInRange(const LevelRangeConfig& range)
{
    return urand(range.lower, range.upper);
}

// Adjusts a bot's level by selecting a random level within the target range.
// Also resets XP, destroys equipped items, removes the pet, and executes maintenance.
static void AdjustBotToRange(Player* bot, int targetRangeIndex, const LevelRangeConfig* factionRanges)
{
    if (!bot || targetRangeIndex < 0 || targetRangeIndex >= NUM_RANGES)
        return;

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
            if (g_BotDistDebugMode)
            {
            	std::string playerFaction = IsAlliancePlayerBot(bot) ? "Alliance" : "Horde";
                LOG_INFO("server.loading",
                         "[BotLevelBrackets] AdjustBotToRange: Cannot assign {} Death Knight '{}' ({}) to range {}-{} (below level 55).",
                         playerFaction, bot->GetName(), botOriginalLevel, lowerBound, upperBound);
            }
            return;
        }
        if (lowerBound < 55)
            lowerBound = 55;
        newLevel = urand(lowerBound, upperBound);
    }
    else
    {
        newLevel = GetRandomLevelInRange(factionRanges[targetRangeIndex]);
    }

    PlayerbotFactory newFactory(bot, newLevel);

    newFactory.Randomize(false);

    if (g_BotDistDebugMode)
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
// BOT INTERFACE HELPERS
// -----------------------------------------------------------------------------

void RemoveAllQuests(Player* bot)
{
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (questId)
            bot->SetQuestSlot(slot, 0);
    }
    CharacterDatabase.Execute("DELETE FROM character_queststatus WHERE guid = {}", bot->GetGUID().GetCounter());
}

void RemoveAllEquippedItems(Player* bot)
{
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
    }
}

void RemoveAllTradeSkills(Player* bot)
{
    static const uint32 tradeSkills[] = {
        SKILL_ALCHEMY, SKILL_BLACKSMITHING, SKILL_COOKING, SKILL_ENCHANTING,
        SKILL_ENGINEERING, SKILL_FIRST_AID, SKILL_FISHING, SKILL_HERBALISM,
        SKILL_JEWELCRAFTING, SKILL_LEATHERWORKING, SKILL_MINING, SKILL_SKINNING,
        SKILL_TAILORING
    };
    for (auto skill : tradeSkills)
    {
        if (bot->HasSkill(skill))
            bot->SetSkill(skill, 0, 0, 0);
    }
}

// -----------------------------------------------------------------------------
// BOT DETECTION HELPERS
// -----------------------------------------------------------------------------
static bool IsPlayerBot(Player* player)
{
    if (!player)
        return false;
    PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(player);
    return botAI && botAI->IsBotAI();
}

static bool IsPlayerRandomBot(Player* player)
{
    if (!player)
        return false;
    return sRandomPlayerbotMgr->IsRandomBot(player);
}

// Helper functions to determine faction. Adjust these functions based on your implementation.
static bool IsAlliancePlayerBot(Player* bot)
{
    // Assumes GetTeam() returns TEAM_ALLIANCE for Alliance bots.
    return bot && (bot->GetTeamId() == TEAM_ALLIANCE);
}

static bool IsHordePlayerBot(Player* bot)
{
    // Assumes GetTeam() returns TEAM_HORDE for Horde bots.
    return bot && (bot->GetTeamId() == TEAM_HORDE);
}

// -----------------------------------------------------------------------------
// SAFETY CHECKS FOR LEVEL RESET
// -----------------------------------------------------------------------------
static bool IsBotSafeForLevelReset(Player* bot)
{
    if (!bot)
        return false;
    if (!bot->IsInWorld())
    	return false;
    if (!bot->IsAlive())
        return false;
    if (bot->IsInCombat())
        return false;
    if (bot->InBattleground() || bot->InArena() || bot->inRandomLfgDungeon() || bot->InBattlegroundQueue())
        return false;
    if (bot->IsInFlight())
    	return false;
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && !IsPlayerBot(member))
                return false;
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
    for (auto it = g_PendingLevelResets.begin(); it != g_PendingLevelResets.end(); )
    {
        Player* bot = it->bot;
        int targetRange = it->targetRange;
        if (bot && bot->IsInWorld() && IsBotSafeForLevelReset(bot))
        {
            AdjustBotToRange(bot, targetRange, it->factionRanges);
            it = g_PendingLevelResets.erase(it);
        }
        else
            ++it;
    }
}

// -----------------------------------------------------------------------------
// WORLD SCRIPT: Bot Level Distribution with Faction Separation
// -----------------------------------------------------------------------------
class BotLevelBracketsWorldScript : public WorldScript
{
public:
    BotLevelBracketsWorldScript() : WorldScript("BotLevelBracketsWorldScript"), m_timer(0), m_flaggedTimer(0) { }

    void OnStartup() override
    {
        LoadBotLevelBracketsConfig();
        if (g_BotDistDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Module loaded. Check frequency: {} seconds, Check flagged frequency: {}.", g_BotDistCheckFrequency, g_BotDistFlaggedCheckFrequency);
            for (uint8 i = 0; i < NUM_RANGES; ++i)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Alliance Range {}: {}-{}, Desired Percentage: {}%",
                         i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper, g_AllianceLevelRanges[i].desiredPercent);
                LOG_INFO("server.loading", "[BotLevelBrackets] Horde Range {}: {}-{}, Desired Percentage: {}%",
                         i + 1, g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper, g_HordeLevelRanges[i].desiredPercent);
            }
        }
    }

    void OnUpdate(uint32 diff) override
    {
        m_timer += diff;
		m_flaggedTimer += diff;

		// Check if it's time to process pending level resets
		if (m_flaggedTimer >= g_BotDistFlaggedCheckFrequency * 1000)
		{
		    ProcessPendingLevelResets();
		    m_flaggedTimer = 0;
		}

		// Continue with distribution adjustments once its timer expires
        if (m_timer < g_BotDistCheckFrequency * 1000)
            return;
        m_timer = 0;

        // Containers for Alliance bots.
        uint32 totalAllianceBots = 0;
        int allianceActualCounts[NUM_RANGES] = {0};
        std::vector<Player*> allianceBotsByRange[NUM_RANGES];

        // Containers for Horde bots.
        uint32 totalHordeBots = 0;
        int hordeActualCounts[NUM_RANGES] = {0};
        std::vector<Player*> hordeBotsByRange[NUM_RANGES];

        // Iterate only over player bots.
        auto const& allPlayers = ObjectAccessor::GetPlayers();
        for (auto const& itr : allPlayers)
        {
            Player* player = itr.second;
            if (!player || !player->IsInWorld())
                continue;
            if (!IsPlayerBot(player) || !IsPlayerRandomBot(player))
                continue;

            if (IsAlliancePlayerBot(player))
            {
                totalAllianceBots++;
                int rangeIndex = GetLevelRangeIndex(player->GetLevel());
                if (rangeIndex >= 0)
                {
                    allianceActualCounts[rangeIndex]++;
                    allianceBotsByRange[rangeIndex].push_back(player);
                }
                else if (g_BotDistDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Alliance bot '{}' with level {} does not fall into any defined range.",
                             player->GetName(), player->GetLevel());
                }
            }
            else if (IsHordePlayerBot(player))
            {
                totalHordeBots++;
                int rangeIndex = GetLevelRangeIndex(player->GetLevel());
                if (rangeIndex >= 0)
                {
                    hordeActualCounts[rangeIndex]++;
                    hordeBotsByRange[rangeIndex].push_back(player);
                }
                else if (g_BotDistDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Horde bot '{}' with level {} does not fall into any defined range.",
                             player->GetName(), player->GetLevel());
                }
            }
        }

        // Process Alliance bots.
        if (totalAllianceBots > 0)
        {
            int allianceDesiredCounts[NUM_RANGES] = {0};
            for (int i = 0; i < NUM_RANGES; ++i)
            {
                allianceDesiredCounts[i] = static_cast<int>(round((g_AllianceLevelRanges[i].desiredPercent / 100.0) * totalAllianceBots));
                if (g_BotDistDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Alliance Range {} ({}-{}): Desired = {}, Actual = {}.",
                             i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper,
                             allianceDesiredCounts[i], allianceActualCounts[i]);
                }
            }
            // Adjust overpopulated ranges.
            for (int i = 0; i < NUM_RANGES; ++i)
            {
                std::vector<Player*> safeBots;
                std::vector<Player*> flaggedBots;
                for (Player* bot : allianceBotsByRange[i])
                {
                    if (IsBotSafeForLevelReset(bot))
                        safeBots.push_back(bot);
                    else
                        flaggedBots.push_back(bot);
                }
                while (allianceActualCounts[i] > allianceDesiredCounts[i] && !safeBots.empty())
                {
                    Player* bot = safeBots.back();
                    safeBots.pop_back();
                    int targetRange = -1;
                    if (bot->getClass() == CLASS_DEATH_KNIGHT)
                    {
                        for (int j = 0; j < NUM_RANGES; ++j)
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
                        for (int j = 0; j < NUM_RANGES; ++j)
                        {
                            if (allianceActualCounts[j] < allianceDesiredCounts[j])
                            {
                                targetRange = j;
                                break;
                            }
                        }
                    }
                    if (targetRange == -1)
                        break;
                    AdjustBotToRange(bot, targetRange, g_AllianceLevelRanges);
                    allianceActualCounts[i]--;
                    allianceActualCounts[targetRange]++;
                }
                while (allianceActualCounts[i] > allianceDesiredCounts[i] && !flaggedBots.empty())
                {
                    Player* bot = flaggedBots.back();
                    flaggedBots.pop_back();
                    int targetRange = -1;
                    if (bot->getClass() == CLASS_DEATH_KNIGHT)
                    {
                        for (int j = 0; j < NUM_RANGES; ++j)
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
                        for (int j = 0; j < NUM_RANGES; ++j)
                        {
                            if (allianceActualCounts[j] < allianceDesiredCounts[j])
                            {
                                targetRange = j;
                                break;
                            }
                        }
                    }
                    if (targetRange == -1)
                        break;
                    // Flag for pending reset.
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
                        g_PendingLevelResets.push_back({bot, targetRange, g_AllianceLevelRanges});
                        if (g_BotDistDebugMode)
                            LOG_INFO("server.loading", "[BotLevelBrackets] Alliance bot '{}' flagged for pending level reset to range {}-{}.",
                                     bot->GetName(), g_AllianceLevelRanges[targetRange].lower, g_AllianceLevelRanges[targetRange].upper);
                    }
                    allianceActualCounts[i]--;
                    allianceActualCounts[targetRange]++;
                }
            }
        }

        // Process Horde bots.
        if (totalHordeBots > 0)
        {
            int hordeDesiredCounts[NUM_RANGES] = {0};
            for (int i = 0; i < NUM_RANGES; ++i)
            {
                hordeDesiredCounts[i] = static_cast<int>(round((g_HordeLevelRanges[i].desiredPercent / 100.0) * totalHordeBots));
                if (g_BotDistDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Horde Range {} ({}-{}): Desired = {}, Actual = {}.",
                             i + 1, g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper,
                             hordeDesiredCounts[i], hordeActualCounts[i]);
                }
            }
            for (int i = 0; i < NUM_RANGES; ++i)
            {
                std::vector<Player*> safeBots;
                std::vector<Player*> flaggedBots;
                for (Player* bot : hordeBotsByRange[i])
                {
                    if (IsBotSafeForLevelReset(bot))
                        safeBots.push_back(bot);
                    else
                        flaggedBots.push_back(bot);
                }
                while (hordeActualCounts[i] > hordeDesiredCounts[i] && !safeBots.empty())
                {
                    Player* bot = safeBots.back();
                    safeBots.pop_back();
                    int targetRange = -1;
                    if (bot->getClass() == CLASS_DEATH_KNIGHT)
                    {
                        for (int j = 0; j < NUM_RANGES; ++j)
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
                        for (int j = 0; j < NUM_RANGES; ++j)
                        {
                            if (hordeActualCounts[j] < hordeDesiredCounts[j])
                            {
                                targetRange = j;
                                break;
                            }
                        }
                    }
                    if (targetRange == -1)
                        break;
                    AdjustBotToRange(bot, targetRange, g_HordeLevelRanges);
                    hordeActualCounts[i]--;
                    hordeActualCounts[targetRange]++;
                }
                while (hordeActualCounts[i] > hordeDesiredCounts[i] && !flaggedBots.empty())
                {
                    Player* bot = flaggedBots.back();
                    flaggedBots.pop_back();
                    int targetRange = -1;
                    if (bot->getClass() == CLASS_DEATH_KNIGHT)
                    {
                        for (int j = 0; j < NUM_RANGES; ++j)
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
                        for (int j = 0; j < NUM_RANGES; ++j)
                        {
                            if (hordeActualCounts[j] < hordeDesiredCounts[j])
                            {
                                targetRange = j;
                                break;
                            }
                        }
                    }
                    if (targetRange == -1)
                        break;
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
                        g_PendingLevelResets.push_back({bot, targetRange, g_HordeLevelRanges});
                        if (g_BotDistDebugMode)
                            LOG_INFO("server.loading", "[BotLevelBrackets] Horde bot '{}' flagged for pending level reset to range {}-{}.",
                                     bot->GetName(), g_HordeLevelRanges[targetRange].lower, g_HordeLevelRanges[targetRange].upper);
                    }
                    hordeActualCounts[i]--;
                    hordeActualCounts[targetRange]++;
                }
            }
        }

        if (g_BotDistDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Distribution adjustment complete. Alliance bots: {}, Horde bots: {}.",
                     totalAllianceBots, totalHordeBots);
        }
    }

private:
    uint32 m_timer;         // For distribution adjustments
    uint32 m_flaggedTimer;  // For pending reset checks
};

// -----------------------------------------------------------------------------
// ENTRY POINT: Register the Bot Level Distribution Module
// -----------------------------------------------------------------------------
void Addmod_player_bot_level_bracketsScripts()
{
    new BotLevelBracketsWorldScript();
}
