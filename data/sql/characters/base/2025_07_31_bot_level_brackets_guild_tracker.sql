-- Bot Level Brackets Guild Tracker Table
-- This table tracks guilds that have real (non-bot) players to prevent bot level changes
-- when real players are in the guild, even when they are offline.

DROP TABLE IF EXISTS `bot_level_brackets_guild_tracker`;

CREATE TABLE `bot_level_brackets_guild_tracker` (
  `guild_id` int(10) unsigned NOT NULL COMMENT 'Guild ID from guild table',
  `has_real_players` tinyint(1) NOT NULL DEFAULT '0' COMMENT 'Whether this guild has real (non-bot) players',
  `last_updated` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT 'Last time this record was updated',
  PRIMARY KEY (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Tracks guilds with real players for bot level bracket restrictions';
