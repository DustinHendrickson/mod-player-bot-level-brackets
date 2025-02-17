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

// -----------------------------------------------------------------------------
// LEVEL RANGE CONFIGURATION
// -----------------------------------------------------------------------------
struct LevelRangeConfig
{
    uint8 lower;         ///< Lower bound (inclusive)
    uint8 upper;         ///< Upper bound (inclusive)
    uint8 desiredPercent;///< Desired percentage of bots in this range
};

static const uint8 NUM_RANGES = 8;
static LevelRangeConfig g_LevelRanges[NUM_RANGES];

static uint32 g_BotDistCheckFrequency = 300; // in seconds
static bool   g_BotDistDebugMode      = false;

// Loads the configuration from the config file.
// Expected keys (with example default percentages):
//   BotDistribution.Range1Pct = 14
//   BotDistribution.Range2Pct = 12
//   BotDistribution.Range3Pct = 12
//   BotDistribution.Range4Pct = 12
//   BotDistribution.Range5Pct = 12
//   BotDistribution.Range6Pct = 12
//   BotDistribution.Range7Pct = 12
//   BotDistribution.Range8Pct = 14
// Additionally:
//   BotDistribution.CheckFrequency (in seconds)
//   BotDistribution.DebugMode (true/false)
static void LoadBotDistributionConfig()
{
    g_BotDistDebugMode = sConfigMgr->GetOption<bool>("BotDistribution.DebugMode", false);
    g_BotDistCheckFrequency = sConfigMgr->GetOption<uint32>("BotDistribution.CheckFrequency", 60);

    g_LevelRanges[0] = { 1, 10,  static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotDistribution.Range1Pct", 14)) };
    g_LevelRanges[1] = { 11, 20, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotDistribution.Range2Pct", 12)) };
    g_LevelRanges[2] = { 21, 30, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotDistribution.Range3Pct", 12)) };
    g_LevelRanges[3] = { 31, 40, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotDistribution.Range4Pct", 12)) };
    g_LevelRanges[4] = { 41, 50, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotDistribution.Range5Pct", 12)) };
    g_LevelRanges[5] = { 51, 60, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotDistribution.Range6Pct", 12)) };
    g_LevelRanges[6] = { 61, 70, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotDistribution.Range7Pct", 12)) };
    g_LevelRanges[7] = { 71, 80, static_cast<uint8>(sConfigMgr->GetOption<uint32>("BotDistribution.Range8Pct", 14)) };

    uint32 totalPercent = 0;
    for (uint8 i = 0; i < NUM_RANGES; ++i)
        totalPercent += g_LevelRanges[i].desiredPercent;
    if (totalPercent != 100)
        LOG_ERROR("server.loading", "[BotLevelBrackets] Sum of percentages is {} (expected 100).", totalPercent);
}

// Returns the index of the level range that the given level belongs to.
// If the level does not fall within any configured range, returns -1.
static int GetLevelRangeIndex(uint8 level)
{
    for (int i = 0; i < NUM_RANGES; ++i)
    {
        if (level >= g_LevelRanges[i].lower && level <= g_LevelRanges[i].upper)
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
// In addition to setting the new level and resetting XP, this function:
//   - Sends a system message indicating the reset.
//   - Destroys all equipped items.
//   - Removes the pet if present.
//   - Executes the auto maintenance action.
static void AdjustBotToRange(Player* bot, int targetRangeIndex)
{
    if (!bot || targetRangeIndex < 0 || targetRangeIndex >= NUM_RANGES)
        return;

    uint8 newLevel = GetRandomLevelInRange(g_LevelRanges[targetRangeIndex]);
    bot->SetLevel(newLevel);
    bot->SetUInt32Value(PLAYER_XP, 0);

    // Inform the bot (or player) about the level reset.
    ChatHandler(bot->GetSession()).SendSysMessage("[mod-bot-level-brackets] Your level has been reset.");

    // Destroy equipped items.
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            std::string itemName = item->GetTemplate()->Name1;
            bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
        }
    }

    // Remove the pet if present.
    if (bot->GetPet())
        bot->RemovePet(nullptr, PET_SAVE_NOT_IN_SLOT, false);

    if (g_BotDistDebugMode)
    {
        PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(bot);
        std::string playerClassName = botAI ? botAI->GetChatHelper()->FormatClass(bot->getClass()) : "Unknown";
        LOG_INFO("server.loading", "[BotLevelBrackets] AdjustBotToRange: Bot '{}' - {} adjusted to level {} (target range {}-{}).",
                 bot->GetName(), playerClassName, newLevel, g_LevelRanges[targetRangeIndex].lower, g_LevelRanges[targetRangeIndex].upper);
    }

    // Execute the maintenance action.
    PlayerbotAI* botAI = sPlayerbotsMgr->GetPlayerbotAI(bot);
    if (botAI)
    {
        AutoMaintenanceOnLevelupAction maintenanceAction(botAI);
        maintenanceAction.Execute(Event());
        if (g_BotDistDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] AdjustBotToRange: AutoMaintenanceOnLevelupAction executed for bot '{}'.", bot->GetName());
    }
    else
    {
        LOG_ERROR("server.loading", "[BotLevelBrackets] AdjustBotToRange: Failed to retrieve PlayerbotAI for bot '{}'.", bot->GetName());
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

// -----------------------------------------------------------------------------
// WORLD SCRIPT: Bot Level Distribution
// -----------------------------------------------------------------------------
class BotLevelBracketsWorldScript : public WorldScript
{
public:
    BotLevelBracketsWorldScript() : WorldScript("BotLevelBracketsWorldScript"), m_timer(0) { }

    // On server startup, load the configuration and log the settings (if debug mode is enabled).
    void OnStartup() override
    {
        LoadBotDistributionConfig();
        if (g_BotDistDebugMode)
        {
            LOG_INFO("server.loading", "[BotLevelBrackets] Module loaded. Check frequency: {} seconds.", g_BotDistCheckFrequency);
            for (uint8 i = 0; i < NUM_RANGES; ++i)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Range {}: {}-{}, Desired Percentage: {}%",
                         i + 1, g_LevelRanges[i].lower, g_LevelRanges[i].upper, g_LevelRanges[i].desiredPercent);
            }
        }
    }

    // Periodically (every g_BotDistCheckFrequency seconds) check the distribution of bot levels
    // and adjust bots from overpopulated ranges to underpopulated ranges.
    void OnUpdate(uint32 diff) override
    {
        m_timer += diff;
        if (m_timer < g_BotDistCheckFrequency * 1000)
            return;
        m_timer = 0;

        // Build the current distribution for bots.
        uint32 totalBots = 0;
        int actualCounts[NUM_RANGES] = {0};
        std::vector<Player*> botsByRange[NUM_RANGES];

        auto const& allPlayers = ObjectAccessor::GetPlayers();
        for (auto const& itr : allPlayers)
        {
            Player* player = itr.second;
            if (!player || !player->IsInWorld())
                continue;
            if (!IsPlayerBot(player) || !IsPlayerRandomBot(player))
                continue;

            totalBots++;
            int rangeIndex = GetLevelRangeIndex(player->GetLevel());
            if (rangeIndex >= 0)
            {
                actualCounts[rangeIndex]++;
                botsByRange[rangeIndex].push_back(player);
            }
            else if (g_BotDistDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Bot '{}' with level {} does not fall into any defined range.",
                         player->GetName(), player->GetLevel());
            }
        }

        if (totalBots == 0)
            return;

        // Compute the desired count for each range.
        int desiredCounts[NUM_RANGES] = {0};
        for (int i = 0; i < NUM_RANGES; ++i)
        {
            desiredCounts[i] = static_cast<int>(round((g_LevelRanges[i].desiredPercent / 100.0) * totalBots));
            if (g_BotDistDebugMode)
            {
                LOG_INFO("server.loading", "[BotLevelBrackets] Range {} ({}-{}): Desired = {}, Actual = {}.",
                         i + 1, g_LevelRanges[i].lower, g_LevelRanges[i].upper,
                         desiredCounts[i], actualCounts[i]);
            }
        }

        // For each range that has a surplus, reassign bots to ranges that are underpopulated.
        for (int i = 0; i < NUM_RANGES; ++i)
        {
            while (actualCounts[i] > desiredCounts[i] && !botsByRange[i].empty())
            {
                // Locate a target range with a deficit.
                int targetRange = -1;
                for (int j = 0; j < NUM_RANGES; ++j)
                {
                    if (actualCounts[j] < desiredCounts[j])
                    {
                        targetRange = j;
                        break;
                    }
                }
                if (targetRange == -1)
                    break; // No underpopulated range found.

                // Retrieve one bot from the current (overpopulated) range.
                Player* bot = botsByRange[i].back();
                botsByRange[i].pop_back();

                // Adjust its level to a random level within the target range and perform cleanup.
                AdjustBotToRange(bot, targetRange);

                actualCounts[i]--;
                actualCounts[targetRange]++;
            }
        }

        if (g_BotDistDebugMode)
            LOG_INFO("server.loading", "[BotLevelBrackets] Distribution adjustment complete. Total bots: {}.", totalBots);
    }

private:
    uint32 m_timer;
};

// -----------------------------------------------------------------------------
// ENTRY POINT: Register the Bot Level Distribution Module
// -----------------------------------------------------------------------------
void Addmod_player_bot_level_bracketsScripts()
{
    new BotLevelBracketsWorldScript();
}
