return {
	version = 1,
	plugins = {
		{
			id = "reverse_probe_runtime",
			channel = "reverse_probe_runtime",
			module = "reverse_probe_runtime",
			scope = "player_preferred",
			playerPriority = 1100,
			fallbackPriority = 1100,
			refreshMode = "manual",
			maxConsecutiveErrors = 1,
			errorCooldownTicks = 64,
			priority = 1100
		},
		{
			id = "native_effect_live_probe",
			channel = "native_effect_live_probe",
			module = "native_effect_live_probe",
			scope = "player_preferred",
			playerPriority = 1000,
			fallbackPriority = 1000,
			refreshMode = "manual",
			maxConsecutiveErrors = 1,
			errorCooldownTicks = 64,
			priority = 1000
		},
		{
			id = "china_anti_jap",
			channel = "china_anti_jap",
			module = "war_map_adapter",
			scope = "player_preferred",
			playerPriority = 100,
			fallbackPriority = 0,
			actionBudget = 64,
			refreshMode = "daily",
			maxConsecutiveErrors = 3,
			errorCooldownTicks = 16
		}
	}
}
