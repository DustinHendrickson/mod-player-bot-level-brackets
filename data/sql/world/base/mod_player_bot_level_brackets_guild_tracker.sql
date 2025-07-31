-- Table structure for tracking guilds with real players for mod-player-bot-level-brackets
-- This table stores guild IDs and whether they contain at least one real (non-bot) player

DROP TABLE IF EXISTS `mod_bot_level_brackets_guild_tracker`;

CREATE TABLE `mod_bot_level_brackets_guild_tracker` (
  `guild_id` int(10) unsigned NOT NULL,
  `has_real_player` tinyint(1) unsigned NOT NULL DEFAULT '0' COMMENT 'Boolean flag: 1 if guild has at least one real player, 0 if only bots',
  `last_updated` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT 'Timestamp of last update',
  PRIMARY KEY (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Tracks guilds with real players for bot level bracket exclusions';
