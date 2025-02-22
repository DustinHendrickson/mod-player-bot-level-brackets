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

static const uint8 NUM_RANGES = 9;

// Global variables to restrict bot levels.
static uint8 g_RandomBotMinLevel = 1;
static uint8 g_RandomBotMaxLevel = 80;


// Separate arrays for Alliance and Horde.
static LevelRangeConfig g_BaseLevelRanges[NUM_RANGES];
static LevelRangeConfig g_AllianceLevelRanges[NUM_RANGES];
static LevelRangeConfig g_HordeLevelRanges[NUM_RANGES];

static uint32 g_BotDistCheckFrequency = 300; // in seconds
static uint32 g_BotDistFlaggedCheckFrequency = 15; // in seconds
static bool   g_BotDistDebugMode      = false;
static bool   g_UseDynamicDistribution  = false;

// Real player weight to boost bracket contributions.
static float g_RealPlayerWeight = 1.0f;

// Loads the configuration from the config file.
static void LoadBotLevelBracketsConfig()
{
    g_BotDistDebugMode = sConfigMgr->GetOption<bool>("BotLevelBrackets.DebugMode", false);
    g_BotDistCheckFrequency = sConfigMgr->GetOption<uint32>("BotLevelBrackets.CheckFrequency", 300);
    g_BotDistFlaggedCheckFrequency = sConfigMgr->GetOption<uint32>("BotLevelBrackets.CheckFlaggedFrequency", 15);
    g_UseDynamicDistribution = sConfigMgr->GetOption<bool>("BotLevelBrackets.UseDynamicDistribution", false);
    g_RealPlayerWeight = sConfigMgr->GetOption<float>("BotLevelBrackets.RealPlayerWeight", 1.0f);

    // Load the bot level restrictions.
    g_RandomBotMinLevel = static_cast<uint8>(sConfigMgr->GetOption<uint32>("AiPlayerbot.RandomBotMinLevel", 1));
    g_RandomBotMaxLevel = static_cast<uint8>(sConfigMgr->GetOption<uint32>("AiPlayerbot.RandomBotMaxLevel", 80));

    // Base Level Ranges
    g_BaseLevelRanges[0] = { 1, 9, 0 };
    g_BaseLevelRanges[1] = { 10, 19, 0};
    g_BaseLevelRanges[2] = { 20, 29, 0};
    g_BaseLevelRanges[3] = { 30, 39, 0};
    g_BaseLevelRanges[4] = { 40, 49, 0};
    g_BaseLevelRanges[5] = { 50, 59, 0};
    g_BaseLevelRanges[6] = { 60, 69, 0};
    g_BaseLevelRanges[7] = { 70, 79, 0};
    g_BaseLevelRanges[8] = { 80, 80, 0};

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

    ClampAndBalanceBrackets();
}

// Returns the index of the level range bracket that the given level belongs to.
static int GetLevelRangeIndex(uint8 level)
{
    for (int i = 0; i < NUM_RANGES; ++i)
    {
        if (level >= g_BaseLevelRanges[i].lower && level <= g_BaseLevelRanges[i].upper)
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

// Helper functions to determine faction.
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

static void ClampAndBalanceBrackets()
{
    // First, adjust Alliance brackets.
    for (uint8 i = 0; i < NUM_RANGES; ++i)
    {
        if (g_AllianceLevelRanges[i].lower < g_RandomBotMinLevel)
            g_AllianceLevelRanges[i].lower = g_RandomBotMinLevel;
        if (g_AllianceLevelRanges[i].upper > g_RandomBotMaxLevel)
            g_AllianceLevelRanges[i].upper = g_RandomBotMaxLevel;
        // If the adjusted bracket is invalid, mark it to not be used.
        if (g_AllianceLevelRanges[i].lower > g_AllianceLevelRanges[i].upper)
            g_AllianceLevelRanges[i].desiredPercent = 0;
    }
    // Then, adjust Horde brackets similarly.
    for (uint8 i = 0; i < NUM_RANGES; ++i)
    {
        if (g_HordeLevelRanges[i].lower < g_RandomBotMinLevel)
            g_HordeLevelRanges[i].lower = g_RandomBotMinLevel;
        if (g_HordeLevelRanges[i].upper > g_RandomBotMaxLevel)
            g_HordeLevelRanges[i].upper = g_RandomBotMaxLevel;
        if (g_HordeLevelRanges[i].lower > g_HordeLevelRanges[i].upper)
            g_HordeLevelRanges[i].desiredPercent = 0;
    }
    // Balance desired percentages so the sum is 100.
    uint32 totalAlliance = 0;
    uint32 totalHorde = 0;
    for (uint8 i = 0; i < NUM_RANGES; ++i)
    {
        totalAlliance += g_AllianceLevelRanges[i].desiredPercent;
        totalHorde += g_HordeLevelRanges[i].desiredPercent;
    }
    // If totals are not 100, then distribute the missing percent among valid brackets.
    if(totalAlliance != 100 && totalAlliance > 0)
    {

        LOG_INFO("server.loading", "[BotLevelBrackets] Alliance: Sum of percentages is {} (expected 100). Auto adjusting.", totalAlliance);
        
        int missing = 100 - totalAlliance;
        while(missing > 0)
        {
            for (uint8 i = 0; i < NUM_RANGES && missing > 0; ++i)
            {
                if(g_AllianceLevelRanges[i].lower <= g_AllianceLevelRanges[i].upper &&
                   g_AllianceLevelRanges[i].desiredPercent > 0)
                {
                    g_AllianceLevelRanges[i].desiredPercent++;
                    missing--;
                }
            }
        }
    }
    if(totalHorde != 100 && totalHorde > 0)
    {
        
        LOG_INFO("server.loading", "[BotLevelBrackets] Horde: Sum of percentages is {} (expected 100). Auto adjusting.", totalHorde);
        
        int missing = 100 - totalHorde;
        while(missing > 0)
        {
            for (uint8 i = 0; i < NUM_RANGES && missing > 0; ++i)
            {
                if(g_HordeLevelRanges[i].lower <= g_HordeLevelRanges[i].upper &&
                   g_HordeLevelRanges[i].desiredPercent > 0)
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
            }
            for (uint8 i = 0; i < NUM_RANGES; ++i)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Horde Range {}: {}-{}, Desired Percentage: {}%",
                         i + 1, g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper, g_HordeLevelRanges[i].desiredPercent);
            }
        }
    }

    void OnUpdate(uint32 diff) override
    {
        m_timer += diff;
        m_flaggedTimer += diff;

        // Process pending level resets.
        if (m_flaggedTimer >= g_BotDistFlaggedCheckFrequency * 1000)
        {
            ProcessPendingLevelResets();
            m_flaggedTimer = 0;
        }

        // Continue with distribution adjustments once the timer expires.
        if (m_timer < g_BotDistCheckFrequency * 1000)
            return;
        m_timer = 0;

        // Dynamic distribution: recalc desired percentages based on non-bot players.
        if (g_UseDynamicDistribution)
        {
            int allianceRealCounts[NUM_RANGES] = {0};
            int hordeRealCounts[NUM_RANGES] = {0};
            uint32 totalAllianceReal = 0;
            uint32 totalHordeReal = 0;
            // Iterate over all players and count non-bot players.
            for (auto const& itr : ObjectAccessor::GetPlayers())
            {
                Player* player = itr.second;
                if (!player || !player->IsInWorld())
                    continue;
                if (IsPlayerBot(player))
                    continue; // Skip bots.
                int rangeIndex = GetLevelRangeIndex(player->GetLevel());
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
            // Use a baseline weight to ensure an equal share when no real players are present.
            const float baseline = 1.0f;
            float allianceTotalWeight = 0.0f;
            float hordeTotalWeight = 0.0f;
            float allianceWeights[NUM_RANGES] = {0};
            float hordeWeights[NUM_RANGES] = {0};
            for (int i = 0; i < NUM_RANGES; ++i)
            {
                // Only count valid brackets.
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

                allianceTotalWeight += allianceWeights[i];
                hordeTotalWeight += hordeWeights[i];
            }
            // Recalculate desired percentages for each range.
            for (int i = 0; i < NUM_RANGES; ++i)
            {
                g_AllianceLevelRanges[i].desiredPercent = static_cast<uint8>(round((allianceWeights[i] / allianceTotalWeight) * 100));
                if (g_BotDistDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Dynamic Distribution - Alliance Range {}: {}-{}, Real Players: {} (weight: {:.2f}), New Desired: {}%",
                             i + 1, g_AllianceLevelRanges[i].lower, g_AllianceLevelRanges[i].upper, allianceRealCounts[i], allianceWeights[i], g_AllianceLevelRanges[i].desiredPercent);
                }
            }

            uint8 sumAlliance = 0;
            for (int i = 0; i < NUM_RANGES; ++i)
                sumAlliance += g_AllianceLevelRanges[i].desiredPercent;
            if (sumAlliance < 100 && allianceTotalWeight > 0)
            {
                uint8 missing = 100 - sumAlliance;
                if (g_BotDistDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Alliance normalization: current sum = {}, missing = {}", sumAlliance, missing);
                }
                while (missing > 0)
                {
                    for (int i = 0; i < NUM_RANGES && missing > 0; ++i)
                    {
                        if (g_AllianceLevelRanges[i].lower <= g_AllianceLevelRanges[i].upper &&
                            allianceWeights[i] > 0)
                        {
                            g_AllianceLevelRanges[i].desiredPercent++;
                            missing--;
                        }
                    }
                }
                if (g_BotDistDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Alliance normalized percentages:");
                    for (int i = 0; i < NUM_RANGES; ++i)
                    {
                        LOG_INFO("server.loading", "    Range {}: {}% ({}-{})", i + 1,
                                 g_AllianceLevelRanges[i].desiredPercent,
                                 g_AllianceLevelRanges[i].lower,
                                 g_AllianceLevelRanges[i].upper);
                    }
                }
            }

            
            for (int i = 0; i < NUM_RANGES; ++i)
            {
                g_HordeLevelRanges[i].desiredPercent = static_cast<uint8>(round((hordeWeights[i] / hordeTotalWeight) * 100));
                if (g_BotDistDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Dynamic Distribution - Horde Range {}: {}-{}, Real Players: {} (weight: {:.2f}), New Desired: {}%",
                             i + 1, g_HordeLevelRanges[i].lower, g_HordeLevelRanges[i].upper, hordeRealCounts[i], hordeWeights[i], g_HordeLevelRanges[i].desiredPercent);
                }
            }

            uint8 sumHorde = 0;
            for (int i = 0; i < NUM_RANGES; ++i)
                sumHorde += g_HordeLevelRanges[i].desiredPercent;
            if (sumHorde < 100 && hordeTotalWeight > 0)
            {
                uint8 missing = 100 - sumHorde;
                if (g_BotDistDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Horde normalization: current sum = {}, missing = {}", sumHorde, missing);
                }
                while (missing > 0)
                {
                    for (int i = 0; i < NUM_RANGES && missing > 0; ++i)
                    {
                        if (g_HordeLevelRanges[i].lower <= g_HordeLevelRanges[i].upper &&
                            hordeWeights[i] > 0)
                        {
                            g_HordeLevelRanges[i].desiredPercent++;
                            missing--;
                        }
                    }
                }
                if (g_BotDistDebugMode)
                {
                    LOG_INFO("server.loading", "[BotLevelBrackets] Horde normalized percentages:");
                    for (int i = 0; i < NUM_RANGES; ++i)
                    {
                        LOG_INFO("server.loading", "    Range {}: {}% ({}-{})", i + 1,
                                 g_HordeLevelRanges[i].desiredPercent,
                                 g_HordeLevelRanges[i].lower,
                                 g_HordeLevelRanges[i].upper);
                    }
                }
            }

        }

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
