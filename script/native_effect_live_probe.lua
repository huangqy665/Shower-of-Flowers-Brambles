local P = {}

local NativeEffects = require('native_effect_bridge')

local function rootPath()
	if os and os.getenv then
		local root = os.getenv("NEW_CORE_ROOT")
		if root and root ~= "" then
			return root
		end
		root = os.getenv("SCRIPTED_GUI_ROOT")
		if root and root ~= "" then
			return root
		end
	end
	return "."
end

local Root = rootPath()
local RequestPath = Root .. "\\new_core\\native_effect_live_probe.request"
local RunningPath = RequestPath .. ".running"
local CompletedPath = RequestPath .. ".completed"
local ReportPath = Root .. "\\new_core\\native_effect_live_probe.log"

local function fileExists(path)
	local file = io.open(path, "r")
	if not file then
		return false
	end
	file:close()
	return true
end

local function readRequestMode()
	local file = io.open(RequestPath, "r")
	if not file then
		return "legacy"
	end
	local mode = file:read("*l")
	file:close()
	if not mode or mode == "" then
		return "legacy"
	end
	return mode
end

local function appendLine(file, text)
	file:write(tostring(text), "\n")
	file:flush()
end

local function safeNumber(reader)
	local success, value = pcall(reader)
	if not success then
		return nil, tostring(value)
	end
	value = tonumber(value)
	if not value then
		return nil, "value_not_numeric"
	end
	return value, nil
end

local function nearlyEqual(first, second)
	return type(first) == "number"
		and type(second) == "number"
		and math.abs(first - second) <= 0.002
end

local function execute(file, operation, arguments, suffix)
	local success, code, message, transactionId =
		NativeEffects.ExecuteEffect(
			operation,
			arguments,
			"native_effect_live_probe" .. (suffix or "")
		)
	appendLine(
		file,
		string.format(
			"EXEC operation=%s success=%s code=%s transaction=%s message=%s",
			operation,
			tostring(success),
			tostring(code),
			tostring(transactionId),
			tostring(message)
		)
	)
	return success, transactionId
end

local testAvailability

local function readGlobalFlag(name)
	local success, value = pcall(function()
		return CCurrentGameState.IsGlobalFlagSet(CString(name))
	end)
	if not success then
		return nil, tostring(value)
	end
	return value == true, nil
end

local function appendFlagCheck(file, name, expected)
	local value, readError = readGlobalFlag(name)
	local passed = value ~= nil and value == expected
	appendLine(
		file,
		string.format(
			"FLAG name=%s value=%s expected=%s read_error=%s passed=%s",
			name,
			tostring(value),
			tostring(expected),
			tostring(readError or ""),
			tostring(passed)
		)
	)
	return passed
end

local DispatchProbeFlags = {
	"new_core_native_global_probe",
	"new_core_native_decision_direct_enable",
	"new_core_native_decision_queue_enable",
	"new_core_native_decision_cancel_enable",
	"new_core_native_event_probe_result",
	"new_core_native_decision_direct_result",
	"new_core_native_decision_queue_result",
	"new_core_native_decision_cancel_result"
}

local function clearDispatchProbeFlags(file, playerTag)
	local passed = true
	for _, flag in ipairs(DispatchProbeFlags) do
		passed = execute(
			file,
			"global.clear_flag",
			{ tag = playerTag, name = flag },
			".cleanup"
		) and passed
	end
	return passed
end

local function runGlobalDispatchStageOne(file, context)
	local operations = {
		"global.set_flag",
		"global.clear_flag",
		"event.fire",
		"event.execute",
		"event.enqueue",
		"event.cancel",
		"decision.execute",
		"decision.enqueue",
		"decision.cancel",
		"queue.cancel"
	}
	local passed = testAvailability(file, operations)
	passed = clearDispatchProbeFlags(file, context.playerTag) and passed

	passed = execute(
		file,
		"global.set_flag",
		{
			tag = context.playerTag,
			name = "new_core_native_global_probe"
		},
		".global_set"
	) and passed
	passed = appendFlagCheck(
		file,
		"new_core_native_global_probe",
		true
	) and passed
	passed = execute(
		file,
		"global.clear_flag",
		{
			tag = context.playerTag,
			name = "new_core_native_global_probe"
		},
		".global_clear"
	) and passed
	passed = appendFlagCheck(
		file,
		"new_core_native_global_probe",
		false
	) and passed

	passed = execute(
		file,
		"global.set_flag",
		{
			tag = context.playerTag,
			name = "new_core_native_decision_direct_enable"
		},
		".decision_direct_enable"
	) and passed
	passed = execute(
		file,
		"decision.execute",
		{
			tag = context.playerTag,
			name = "new_core_native_decision_direct_probe"
		},
		".decision_direct"
	) and passed
	passed = appendFlagCheck(
		file,
		"new_core_native_decision_direct_result",
		true
	) and passed
	passed = execute(
		file,
		"global.clear_flag",
		{
			tag = context.playerTag,
			name = "new_core_native_decision_direct_enable"
		},
		".decision_direct_disable"
	) and passed

	local eventQueued, eventTransaction = execute(
		file,
		"event.enqueue",
		{
			tag = context.playerTag,
			id = 20990001,
			delay_days = 30
		},
		".event_cancel_enqueue"
	)
	local eventCanceled = eventQueued and execute(
		file,
		"event.cancel",
		{ transaction_id = eventTransaction },
		".event_cancel"
	)
	passed = eventCanceled and passed

	local decisionQueued, decisionTransaction = execute(
		file,
		"decision.enqueue",
		{
			tag = context.playerTag,
			name = "new_core_native_decision_cancel_probe",
			delay_days = 30
		},
		".decision_cancel_enqueue"
	)
	local decisionCanceled = decisionQueued and execute(
		file,
		"decision.cancel",
		{ transaction_id = decisionTransaction },
		".decision_cancel"
	)
	passed = decisionCanceled and passed

	local genericQueued, genericTransaction = execute(
		file,
		"event.enqueue",
		{
			tag = context.playerTag,
			id = 20990001,
			delay_days = 30
		},
		".generic_cancel_enqueue"
	)
	local genericCanceled = genericQueued and execute(
		file,
		"queue.cancel",
		{ transaction_id = genericTransaction },
		".generic_cancel"
	)
	passed = genericCanceled and passed

	passed = execute(
		file,
		"global.set_flag",
		{
			tag = context.playerTag,
			name = "new_core_native_decision_queue_enable"
		},
		".decision_queue_enable"
	) and passed
	local queuedForTomorrow, tomorrowTransaction = execute(
		file,
		"decision.enqueue",
		{
			tag = context.playerTag,
			name = "new_core_native_decision_queue_probe",
			delay_days = 1
		},
		".decision_tomorrow"
	)
	appendLine(
		file,
		"PENDING decision_transaction="
			.. tostring(tomorrowTransaction)
	)
	passed = queuedForTomorrow and passed

	passed = execute(
		file,
		"event.fire",
		{ tag = context.playerTag, id = 20990001 },
		".event_fire"
	) and passed
	appendLine(file, "NEXT click the probe event option and run one day")
	appendLine(file, "RESULT stage=1 passed=" .. tostring(passed))
	return passed
end

local function runGlobalDispatchStageTwo(file, context)
	local passed = appendFlagCheck(
		file,
		"new_core_native_event_probe_result",
		true
	)
	passed = appendFlagCheck(
		file,
		"new_core_native_decision_queue_result",
		true
	) and passed
	passed = appendFlagCheck(
		file,
		"new_core_native_decision_cancel_result",
		false
	) and passed
	passed = clearDispatchProbeFlags(file, context.playerTag) and passed
	for _, flag in ipairs(DispatchProbeFlags) do
		passed = appendFlagCheck(file, flag, false) and passed
	end
	appendLine(file, "RESULT stage=2 passed=" .. tostring(passed))
	return passed
end

local function testSet(file, operation, arguments, reader)
	local before, readError = safeNumber(reader)
	if not before then
		appendLine(file, "FAIL " .. operation .. " read=" .. readError)
		return false
	end
	arguments.value = before
	local applied = execute(file, operation, arguments, ".set")
	local after, afterError = safeNumber(reader)
	local passed = applied and after and nearlyEqual(before, after)
	appendLine(
		file,
		string.format(
			"CHECK operation=%s before=%s after=%s read_error=%s passed=%s",
			operation,
			tostring(before),
			tostring(after),
			tostring(afterError or ""),
			tostring(passed)
		)
	)
	return passed
end

local function testAddAndRestore(
	file,
	operation,
	arguments,
	reader,
	delta,
	requireVisibleChange
)
	local before, readError = safeNumber(reader)
	if not before then
		appendLine(file, "FAIL " .. operation .. " read=" .. readError)
		return false
	end
	arguments.amount = delta
	local added = execute(file, operation, arguments, ".add")
	local middle, middleError = safeNumber(reader)
	arguments.amount = -delta
	local restored = added and execute(
		file,
		operation,
		arguments,
		".restore"
	)
	local after, afterError = safeNumber(reader)
	local changed = middle and not nearlyEqual(before, middle)
	local passed = added and restored
		and (requireVisibleChange == false or changed)
		and after and nearlyEqual(before, after)
	appendLine(
		file,
		string.format(
			"CHECK operation=%s before=%s middle=%s after=%s middle_error=%s after_error=%s passed=%s",
			operation,
			tostring(before),
			tostring(middle),
			tostring(after),
			tostring(middleError or ""),
			tostring(afterError or ""),
			tostring(passed)
		)
	)
	return passed
end

testAvailability = function(file, operations)
	local passed = true
	for _, operation in ipairs(operations) do
		local available = NativeEffects.IsAvailable(operation)
		appendLine(
			file,
			"AVAILABLE operation=" .. operation
				.. " value=" .. tostring(available)
		)
		passed = available and passed
	end
	return passed
end

local function testNamedSet(
	file,
	operation,
	arguments,
	reader
)
	local readSuccess, before = pcall(reader)
	if not readSuccess or not before or before == "" then
		appendLine(
			file,
			"FAIL " .. operation .. " read=" .. tostring(before)
		)
		return false
	end
	local applied = execute(file, operation, arguments, ".set")
	local afterSuccess, after = pcall(reader)
	local passed = applied and afterSuccess and before == after
	appendLine(
		file,
		string.format(
			"CHECK operation=%s before=%s after=%s passed=%s",
			operation,
			tostring(before),
			tostring(after),
			tostring(passed)
		)
	)
	return passed
end

local function runHighPriorityNoopProbe(file, context, country)
	local passed = true
	local capital = country:GetCapitalLocation()
	local capitalId = capital:GetProvinceID()
	local ownerTag = tostring(capital:GetOwner())
	local controllerTag = tostring(capital:GetController())

	passed = testNamedSet(
		file,
		"province.set_owner",
		{
			province_id = capitalId,
			owner = ownerTag
		},
		function()
			return tostring(capital:GetOwner())
		end
	) and passed
	passed = testNamedSet(
		file,
		"province.set_controller",
		{
			province_id = capitalId,
			controller = controllerTag
		},
		function()
			return tostring(capital:GetController())
		end
	) and passed

	local buildingNames = {
		"industry",
		"infra",
		"air_base",
		"naval_base",
		"anti_air",
		"land_fort"
	}
	local selectedBuilding = nil
	local selectedLevel = nil
	for _, buildingName in ipairs(buildingNames) do
		local readSuccess, current, maximum = pcall(function()
			local building = CBuildingDataBase.GetBuilding(buildingName)
			local state = capital:GetBuilding(building)
			return state:GetCurrent():Get(), state:GetMax():Get()
		end)
		current = tonumber(current)
		maximum = tonumber(maximum)
		if readSuccess
			and current
			and maximum
			and nearlyEqual(current, maximum)
			and maximum >= 0
			and maximum <= 100
			and nearlyEqual(maximum, math.floor(maximum + 0.5)) then
			selectedBuilding = buildingName
			selectedLevel = math.floor(maximum + 0.5)
			break
		end
	end
	if selectedBuilding then
		local applied = execute(
			file,
			"province.set_building_level",
			{
				province_id = capitalId,
				building = selectedBuilding,
				level = selectedLevel
			},
			".noop"
		)
		local readSuccess, current, maximum = pcall(function()
			local building = CBuildingDataBase.GetBuilding(selectedBuilding)
			local state = capital:GetBuilding(building)
			return state:GetCurrent():Get(), state:GetMax():Get()
		end)
		local unchanged = readSuccess
			and nearlyEqual(tonumber(current), selectedLevel)
			and nearlyEqual(tonumber(maximum), selectedLevel)
		local checkPassed = applied and unchanged
		appendLine(
			file,
			string.format(
				"CHECK operation=province.set_building_level province=%s building=%s level=%s current=%s maximum=%s passed=%s",
				tostring(capitalId),
				tostring(selectedBuilding),
				tostring(selectedLevel),
				tostring(current),
				tostring(maximum),
				tostring(checkPassed)
			)
		)
		passed = checkPassed and passed
	else
		appendLine(file, "FAIL province.set_building_level no_stable_capital_building")
		passed = false
	end

	local activeResearch = {}
	for technology in country:GetCurrentResearch() do
		local key = tostring(technology:GetKey())
		activeResearch[key] = true
		appendLine(file, "RESEARCH active=" .. key)
	end
	local technologyNames = {
		"industral_production",
		"industral_efficiency",
		"education",
		"superior_firepower",
		"militia_smallarms"
	}
	local selectedTechnology = nil
	local selectedTechnologyObject = nil
	for _, technologyName in ipairs(technologyNames) do
		local lookupSuccess, technology = pcall(
			CTechnologyDataBase.GetTechnology,
			technologyName
		)
		local validSuccess, valid = pcall(function()
			return technology and technology:IsValid()
		end)
		if lookupSuccess
			and validSuccess
			and valid
			and not activeResearch[technologyName] then
			selectedTechnology = technologyName
			selectedTechnologyObject = technology
			break
		end
	end
	if selectedTechnology then
		local technologyStatus = country:GetTechnologyStatus()
		local before = technologyStatus:GetLevel(selectedTechnologyObject)
		local applied = execute(
			file,
			"technology.set_level",
			{
				tag = context.playerTag,
				technology = selectedTechnology,
				level = before
			},
			".noop"
		)
		local after = technologyStatus:GetLevel(selectedTechnologyObject)
		local checkPassed = applied and before == after
		appendLine(
			file,
			string.format(
				"CHECK operation=technology.set_level technology=%s before=%s after=%s passed=%s",
				tostring(selectedTechnology),
				tostring(before),
				tostring(after),
				tostring(checkPassed)
			)
		)
		passed = checkPassed and passed
	else
		appendLine(file, "FAIL technology.set_level no_inactive_probe_technology")
		passed = false
	end

	passed = testSet(
		file,
		"country.set_capital",
		{
			tag = context.playerTag,
			province_id = capitalId
		},
		function()
			return country:GetCapitalLocation():GetProvinceID()
		end
	) and passed

	local actingCapital = country:GetActingCapitalLocation()
	local actingCapitalId = actingCapital:GetProvinceID()
	passed = testSet(
		file,
		"country.set_acting_capital",
		{
			tag = context.playerTag,
			province_id = actingCapitalId
		},
		function()
			return country:GetActingCapitalLocation():GetProvinceID()
		end
	) and passed

	appendLine(file, "RESULT passed=" .. tostring(passed))
	return passed
end

local function executeExpectedRollback(file, label, effects)
	local success, code, message, transactionId =
		NativeEffects.ExecuteTransaction(
			"native_effect_live_probe." .. label,
			effects,
			true
		)
	local passed = not success
		and code == "native_effect_apply_failed"
	appendLine(
		file,
		string.format(
			"ROLLBACK label=%s success=%s code=%s transaction=%s message=%s passed=%s",
			label,
			tostring(success),
			tostring(code),
			tostring(transactionId),
			tostring(message),
			tostring(passed)
		)
	)
	return passed
end

local function countryHasCore(country, provinceId)
	for coreProvinceId in country:GetCoreProvinces() do
		if coreProvinceId == provinceId then
			return true
		end
	end
	return false
end

local function selectRollbackProvince(country, capitalId, playerTag)
	for provinceId in country:GetOwnedProvinces() do
		if provinceId ~= capitalId then
			local success, province = pcall(
				CCurrentGameState.GetProvince,
				provinceId
			)
			if success
				and province
				and tostring(province:GetOwner()) == playerTag
				and tostring(province:GetController()) == playerTag
				and province:GetNumberOfUnits() == 0
				and countryHasCore(country, provinceId) then
				return province
			end
		end
	end
	return nil
end

local function selectAlternateCountry(provinceId, playerTag)
	local candidates = { "USA", "ENG", "SOV" }
	for _, tagName in ipairs(candidates) do
		if tagName ~= playerTag then
			local success, country = pcall(function()
				local tag = CCountryDataBase.GetTag(tagName)
				local candidate = tag:GetCountry()
				if candidate:Exists()
					and not countryHasCore(candidate, provinceId) then
					return candidate
				end
				return nil
			end)
			if success and country then
				return tagName, country
			end
		end
	end
	return nil, nil
end

local function readBuildingState(province, buildingName)
	local building = CBuildingDataBase.GetBuilding(buildingName)
	local state = province:GetBuilding(building)
	return tonumber(state:GetCurrent():Get()),
		tonumber(state:GetMax():Get())
end

local function selectRollbackBuilding(province)
	local names = {
		"industry",
		"infra",
		"air_base",
		"naval_base",
		"anti_air",
		"land_fort"
	}
	for _, name in ipairs(names) do
		local success, current, maximum = pcall(
			readBuildingState,
			province,
			name
		)
		if success
			and current
			and maximum
			and nearlyEqual(current, maximum)
			and nearlyEqual(maximum, math.floor(maximum + 0.5))
			and maximum >= 0
			and maximum <= 100 then
			return name, math.floor(maximum + 0.5)
		end
	end
	return nil, nil
end

local function getActiveResearch(country)
	for technology in country:GetCurrentResearch() do
		return technology, tostring(technology:GetKey())
	end
	return nil, nil
end

local function isResearchActive(country, technologyName)
	for technology in country:GetCurrentResearch() do
		if tostring(technology:GetKey()) == technologyName then
			return true
		end
	end
	return false
end

local function runHighPriorityRollbackProbe(file, context, country)
	local passed = true
	local capital = country:GetCapitalLocation()
	local capitalId = capital:GetProvinceID()
	local province = selectRollbackProvince(
		country,
		capitalId,
		context.playerTag
	)
	if not province then
		appendLine(file, "FAIL rollback no_safe_owned_core_province")
		appendLine(file, "RESULT passed=false")
		return false
	end
	local provinceId = province:GetProvinceID()
	local originalOwner = tostring(province:GetOwner())
	local originalController = tostring(province:GetController())
	local alternateTag, alternateCountry = selectAlternateCountry(
		provinceId,
		context.playerTag
	)
	if not alternateTag then
		appendLine(file, "FAIL rollback no_alternate_country")
		appendLine(file, "RESULT passed=false")
		return false
	end
	appendLine(
		file,
		"TARGET province=" .. tostring(provinceId)
			.. " alternate=" .. alternateTag
	)

	local ownerRolledBack = executeExpectedRollback(
		file,
		"province_owner",
		{
			{
				operation = "province.set_owner",
				arguments = {
					province_id = provinceId,
					owner = alternateTag
				}
			},
			{
				operation = "province.set_owner",
				arguments = {
					province_id = provinceId,
					owner = originalOwner
				}
			}
		}
	)
	local ownerRestored = tostring(province:GetOwner()) == originalOwner
		and tostring(province:GetController()) == originalController
	appendLine(file, "VERIFY province_owner_restored=" .. tostring(ownerRestored))
	passed = ownerRolledBack and ownerRestored and passed

	local controllerRolledBack = executeExpectedRollback(
		file,
		"province_controller",
		{
			{
				operation = "province.set_controller",
				arguments = {
					province_id = provinceId,
					controller = alternateTag
				}
			},
			{
				operation = "province.set_controller",
				arguments = {
					province_id = provinceId,
					controller = originalController
				}
			}
		}
	)
	local controllerRestored = tostring(province:GetController())
		== originalController
	appendLine(
		file,
		"VERIFY province_controller_restored="
			.. tostring(controllerRestored)
	)
	passed = controllerRolledBack and controllerRestored and passed

	local addCoreRolledBack = executeExpectedRollback(
		file,
		"province_add_core",
		{
			{
				operation = "province.add_core",
				arguments = {
					province_id = provinceId,
					core = alternateTag
				}
			},
			{
				operation = "province.add_core",
				arguments = {
					province_id = provinceId,
					core = alternateTag
				}
			}
		}
	)
	local addedCoreRestored = not countryHasCore(
		alternateCountry,
		provinceId
	)
	appendLine(
		file,
		"VERIFY province_added_core_absent="
			.. tostring(addedCoreRestored)
	)
	passed = addCoreRolledBack and addedCoreRestored and passed

	local removeCoreRolledBack = executeExpectedRollback(
		file,
		"province_remove_core",
		{
			{
				operation = "province.remove_core",
				arguments = {
					province_id = provinceId,
					core = context.playerTag
				}
			},
			{
				operation = "province.remove_core",
				arguments = {
					province_id = provinceId,
					core = context.playerTag
				}
			}
		}
	)
	local removedCoreRestored = countryHasCore(country, provinceId)
	appendLine(
		file,
		"VERIFY province_removed_core_present="
			.. tostring(removedCoreRestored)
	)
	passed = removeCoreRolledBack and removedCoreRestored and passed

	local buildingName, buildingLevel = selectRollbackBuilding(province)
	if buildingName then
		local targetLevel = buildingLevel < 100
			and buildingLevel + 1
			or buildingLevel - 1
		local buildingRolledBack = executeExpectedRollback(
			file,
			"province_building",
			{
				{
					operation = "province.set_building_level",
					arguments = {
						province_id = provinceId,
						building = buildingName,
						level = targetLevel
					}
				},
				{
					operation = "province.set_building_level",
					arguments = {
						province_id = provinceId,
						building = buildingName,
						level = targetLevel
					}
				}
			}
		)
		local readSuccess, current, maximum = pcall(
			readBuildingState,
			province,
			buildingName
		)
		local buildingRestored = readSuccess
			and nearlyEqual(current, buildingLevel)
			and nearlyEqual(maximum, buildingLevel)
		appendLine(
			file,
			"VERIFY province_building_restored="
				.. tostring(buildingRestored)
		)
		passed = buildingRolledBack and buildingRestored and passed
	else
		appendLine(file, "FAIL rollback no_stable_building")
		passed = false
	end

	local technologyName, technologyObject = nil, nil
	local activeTechnology = getActiveResearch(country)
	local activeName = activeTechnology
		and tostring(activeTechnology:GetKey())
		or ""
	local technologyNames = {
		"industral_production",
		"industral_efficiency",
		"education",
		"superior_firepower",
		"militia_smallarms"
	}
	for _, candidate in ipairs(technologyNames) do
		if candidate ~= activeName then
			local lookupSuccess, candidateObject = pcall(
				CTechnologyDataBase.GetTechnology,
				candidate
			)
			local validSuccess, valid = pcall(function()
				return candidateObject and candidateObject:IsValid()
			end)
			if lookupSuccess and validSuccess and valid then
				technologyName = candidate
				technologyObject = candidateObject
				break
			end
		end
	end
	if technologyName then
		local status = country:GetTechnologyStatus()
		local oldLevel = status:GetLevel(technologyObject)
		local targetLevel = oldLevel == 0 and 1 or oldLevel - 1
		local technologyRolledBack = executeExpectedRollback(
			file,
			"technology_level",
			{
				{
					operation = "technology.set_level",
					arguments = {
						tag = context.playerTag,
						technology = technologyName,
						level = targetLevel
					}
				},
				{
					operation = "technology.set_level",
					arguments = {
						tag = context.playerTag,
						technology = technologyName,
						level = targetLevel
					}
				}
			}
		)
		local technologyRestored = status:GetLevel(technologyObject)
			== oldLevel
		appendLine(
			file,
			"VERIFY technology_level_restored="
				.. tostring(technologyRestored)
		)
		passed = technologyRolledBack and technologyRestored and passed
	else
		appendLine(file, "FAIL rollback no_technology")
		passed = false
	end

	local originalCapitalId = country:GetCapitalLocation():GetProvinceID()
	local originalActingId = country:GetActingCapitalLocation():GetProvinceID()
	local capitalRolledBack = executeExpectedRollback(
		file,
		"country_capital",
		{
			{
				operation = "country.set_capital",
				arguments = {
					tag = context.playerTag,
					province_id = provinceId
				}
			},
			{
				operation = "country.set_capital",
				arguments = {
					tag = context.playerTag,
					province_id = originalCapitalId
				}
			}
		}
	)
	local capitalRestored = country:GetCapitalLocation():GetProvinceID()
		== originalCapitalId
		and country:GetActingCapitalLocation():GetProvinceID()
		== originalActingId
	appendLine(file, "VERIFY country_capital_restored=" .. tostring(capitalRestored))
	passed = capitalRolledBack and capitalRestored and passed

	local actingRolledBack = executeExpectedRollback(
		file,
		"country_acting_capital",
		{
			{
				operation = "country.set_acting_capital",
				arguments = {
					tag = context.playerTag,
					province_id = provinceId
				}
			},
			{
				operation = "country.set_acting_capital",
				arguments = {
					tag = context.playerTag,
					province_id = originalActingId
				}
			}
		}
	)
	local actingRestored = country:GetActingCapitalLocation():GetProvinceID()
		== originalActingId
	appendLine(
		file,
		"VERIFY country_acting_capital_restored="
			.. tostring(actingRestored)
	)
	passed = actingRolledBack and actingRestored and passed

	appendLine(file, "RESULT passed=" .. tostring(passed))
	return passed
end

local function runHighPriorityResearchRollbackProbe(file, context, country)
	local technology, technologyName = getActiveResearch(country)
	if not technology then
		appendLine(file, "FAIL research no_active_research")
		appendLine(file, "RESULT passed=false")
		return false
	end
	local status = country:GetTechnologyStatus()
	local oldLevel = status:GetLevel(technology)
	local passed = true
	appendLine(
		file,
		"TARGET research=" .. technologyName
			.. " level=" .. tostring(oldLevel)
	)

	local progressRolledBack = executeExpectedRollback(
		file,
		"research_progress",
		{
			{
				operation = "research.set_progress",
				arguments = {
					tag = context.playerTag,
					technology = technologyName,
					progress = 0
				}
			},
			{
				operation = "technology.set_level",
				arguments = {
					tag = context.playerTag,
					technology = technologyName,
					level = oldLevel
				}
			}
		}
	)
	local progressStateRestored = isResearchActive(
		country,
		technologyName
	) and status:GetLevel(technology) == oldLevel
	appendLine(
		file,
		"VERIFY research_progress_state_restored="
			.. tostring(progressStateRestored)
	)
	passed = progressRolledBack and progressStateRestored and passed

	local cancelRolledBack = executeExpectedRollback(
		file,
		"research_cancel",
		{
			{
				operation = "research.cancel",
				arguments = {
					tag = context.playerTag,
					technology = technologyName
				}
			},
			{
				operation = "research.set_progress",
				arguments = {
					tag = context.playerTag,
					technology = technologyName,
					progress = 0
				}
			}
		}
	)
	local cancelStateRestored = isResearchActive(
		country,
		technologyName
	) and status:GetLevel(technology) == oldLevel
	appendLine(
		file,
		"VERIFY research_cancel_state_restored="
			.. tostring(cancelStateRestored)
	)
	passed = cancelRolledBack and cancelStateRestored and passed

	local completeRolledBack = executeExpectedRollback(
		file,
		"research_complete",
		{
			{
				operation = "research.complete",
				arguments = {
					tag = context.playerTag,
					technology = technologyName
				}
			},
			{
				operation = "research.set_progress",
				arguments = {
					tag = context.playerTag,
					technology = technologyName,
					progress = 0
				}
			}
		}
	)
	local completeStateRestored = isResearchActive(
		country,
		technologyName
	) and status:GetLevel(technology) == oldLevel
	appendLine(
		file,
		"VERIFY research_complete_state_restored="
			.. tostring(completeStateRestored)
	)
	passed = completeRolledBack and completeStateRestored and passed

	appendLine(file, "RESULT passed=" .. tostring(passed))
	return passed
end

local function runProbe(context)
	local mode = readRequestMode()
	os.remove(CompletedPath)
	if not os.rename(RequestPath, RunningPath) then
		return false
	end

	local file = io.open(ReportPath, "w")
	if not file then
		os.rename(RunningPath, RequestPath)
		return false
	end
	appendLine(file, "HOI3 native effect live probe")
	appendLine(file, "player=" .. tostring(context.playerTag))
	appendLine(file, "day=" .. tostring(context.currentDay))
	appendLine(file, "mode=" .. tostring(mode))
	appendLine(file, "thread_claim=runtime_player_publisher")

	local country = context.playerTagObject:GetCountry()
	if mode == "global_dispatch_stage1" then
		runGlobalDispatchStageOne(file, context)
		file:close()
		os.rename(RunningPath, CompletedPath)
		P.Completed = true
		return true
	end
	if mode == "global_dispatch_stage2" then
		runGlobalDispatchStageTwo(file, context)
		file:close()
		os.rename(RunningPath, CompletedPath)
		P.Completed = true
		return true
	end
	if mode == "high_priority_noop" then
		runHighPriorityNoopProbe(file, context, country)
		file:close()
		os.rename(RunningPath, CompletedPath)
		P.Completed = true
		return true
	end
	if mode == "high_priority_rollback" then
		runHighPriorityRollbackProbe(file, context, country)
		file:close()
		os.rename(RunningPath, CompletedPath)
		P.Completed = true
		return true
	end
	if mode == "high_priority_research_rollback" then
		runHighPriorityResearchRollbackProbe(file, context, country)
		file:close()
		os.rename(RunningPath, CompletedPath)
		P.Completed = true
		return true
	end
	local pool = country:GetPool()
	local operations = {
		"province.set_owner",
		"province.set_controller",
		"province.add_core",
		"province.remove_core",
		"province.set_building_level",
		"technology.set_level",
		"research.set_progress",
		"research.complete",
		"research.cancel",
		"country.set_capital",
		"country.set_acting_capital",
		"country.add_manpower",
		"country.set_manpower",
		"country.add_goods",
		"country.set_goods",
		"country.add_national_unity",
		"country.set_national_unity",
		"country.add_dissent",
		"country.set_dissent",
		"country.add_neutrality",
		"country.set_neutrality",
		"country.add_officers",
		"country.set_officers",
		"country.add_diplomatic_influence",
		"country.set_diplomatic_influence",
		"country.add_leadership",
		"country.set_leadership",
		"country.add_convoys",
		"country.set_convoys",
		"country.add_escorts",
		"country.set_escorts",
		"country.add_free_spies",
		"country.set_free_spies",
		"country.set_government",
		"country.set_ruling_ideology",
		"country.add_ideology_popularity",
		"country.set_ideology_popularity",
		"country.add_ideology_organization",
		"country.set_ideology_organization",
		"diplomacy.add_relation",
		"diplomacy.set_relation",
		"diplomacy.add_threat",
		"diplomacy.set_threat",
		"espionage.set_presence_level",
		"intelligence.set_province_level",
		"country.add_modifier",
		"country.remove_modifier",
		"province.add_modifier",
		"province.remove_modifier"
	}
	local passed = testAvailability(file, operations)

	local countryArguments = { tag = context.playerTag }
	passed = testSet(
		file,
		"country.set_manpower",
		{ tag = context.playerTag },
		function() return country:GetManpower():Get() end
	) and passed
	passed = testAddAndRestore(
		file,
		"country.add_manpower",
		countryArguments,
		function() return country:GetManpower():Get() end,
		0.01
	) and passed

	local goods = {
		{ name = "supplies", key = CGoodsPool._SUPPLIES_ },
		{ name = "fuel", key = CGoodsPool._FUEL_ },
		{ name = "money", key = CGoodsPool._MONEY_ },
		{ name = "crude_oil", key = CGoodsPool._CRUDE_OIL_ },
		{ name = "metal", key = CGoodsPool._METAL_ },
		{ name = "energy", key = CGoodsPool._ENERGY_ },
		{ name = "rare_materials", key = CGoodsPool._RARE_MATERIALS_ }
	}
	for _, goodsEntry in ipairs(goods) do
		local reader = function()
			return pool:Get(goodsEntry.key):Get()
		end
		passed = testSet(
			file,
			"country.set_goods",
			{ tag = context.playerTag, goods = goodsEntry.name },
			reader
		) and passed
		passed = testAddAndRestore(
			file,
			"country.add_goods",
			{ tag = context.playerTag, goods = goodsEntry.name },
			reader,
			0.01
		) and passed
	end

	local nationalUnityReader = function()
		return country:GetNationalUnity():Get()
	end
	local nationalUnity = nationalUnityReader()
	local nationalUnityDelta = nationalUnity >= 99 and -1 or 1
	passed = testSet(
		file,
		"country.set_national_unity",
		{ tag = context.playerTag },
		nationalUnityReader
	) and passed
	passed = testAddAndRestore(
		file,
		"country.add_national_unity",
		{ tag = context.playerTag },
		nationalUnityReader,
		nationalUnityDelta
	) and passed

	local dissentReader = function() return country:GetDissent():Get() end
	local dissent = dissentReader()
	local dissentDelta = dissent >= 99.99 and -0.01 or 0.01
	passed = testSet(
		file,
		"country.set_dissent",
		{ tag = context.playerTag },
		dissentReader
	) and passed
	passed = testAddAndRestore(
		file,
		"country.add_dissent",
		{ tag = context.playerTag },
		dissentReader,
		dissentDelta
	) and passed

	local neutralityReader = function()
		return country:GetNeutrality():Get()
	end
	local neutrality = neutralityReader()
	local neutralityDelta = neutrality >= 99.99 and -0.01 or 0.01
	passed = testSet(
		file,
		"country.set_neutrality",
		{ tag = context.playerTag },
		neutralityReader
	) and passed
	passed = testAddAndRestore(
		file,
		"country.add_neutrality",
		{ tag = context.playerTag },
		neutralityReader,
		neutralityDelta
	) and passed

	local officerReader = function()
		return country:GetOfficerRatio():Get()
	end
	passed = testAddAndRestore(
		file,
		"country.add_officers",
		{ tag = context.playerTag },
		officerReader,
		1,
		false
	) and passed

	local diplomaticInfluenceReader = function()
		return country:GetDiplomaticInfluence():Get()
	end
	passed = testSet(
		file,
		"country.set_diplomatic_influence",
		{ tag = context.playerTag },
		diplomaticInfluenceReader
	) and passed
	passed = testAddAndRestore(
		file,
		"country.add_diplomatic_influence",
		{ tag = context.playerTag },
		diplomaticInfluenceReader,
		0.01
	) and passed

	passed = testSet(
		file,
		"country.set_leadership",
		{ tag = context.playerTag },
		function() return country:GetTotalLeadership():Get() end
	) and passed
	passed = testAddAndRestore(
		file,
		"country.add_leadership",
		{ tag = context.playerTag },
		function() return country:GetTotalLeadership():Get() end,
		0.01
	) and passed

	local convoyReader = function()
		return country:GetTransports()
	end
	passed = testSet(
		file,
		"country.set_convoys",
		{ tag = context.playerTag },
		convoyReader
	) and passed
	passed = testAddAndRestore(
		file,
		"country.add_convoys",
		{ tag = context.playerTag },
		convoyReader,
		1
	) and passed

	local escortReader = function()
		return country:GetEscorts()
	end
	passed = testSet(
		file,
		"country.set_escorts",
		{ tag = context.playerTag },
		escortReader
	) and passed
	passed = testAddAndRestore(
		file,
		"country.add_escorts",
		{ tag = context.playerTag },
		escortReader,
		1
	) and passed

	local freeSpyReader = function()
		return country:GetNumberOfFreeSpies()
	end
	passed = testSet(
		file,
		"country.set_free_spies",
		{ tag = context.playerTag },
		freeSpyReader
	) and passed
	passed = testAddAndRestore(
		file,
		"country.add_free_spies",
		{ tag = context.playerTag },
		freeSpyReader,
		1
	) and passed

	local governmentNames = {
		USA = "social_liberalism",
		CHI = "social_conservatism",
		CHC = "chc_socialist_republic",
		JAP = "imperial"
	}
	local governmentName = governmentNames[context.playerTag]
	if governmentName then
		passed = testNamedSet(
			file,
			"country.set_government",
			{ tag = context.playerTag, government = governmentName },
			function()
				return country:GetGovernment():IsValid()
					and "valid" or "invalid"
			end
		) and passed
	else
		appendLine(
			file,
			"SKIP operation=country.set_government reason=unknown_current_definition"
		)
	end

	local rulingIdeology = country:GetRulingIdeology()
	local rulingIdeologyName = tostring(rulingIdeology:GetKey())
	passed = testNamedSet(
		file,
		"country.set_ruling_ideology",
		{ tag = context.playerTag, ideology = rulingIdeologyName },
		function()
			return tostring(country:GetRulingIdeology():GetKey())
		end
	) and passed

	local popularityReader = function()
		return country:AccessIdeologyPopularity()
			:GetValue(country:GetRulingIdeology()):Get()
	end
	local popularity = popularityReader()
	local popularityDelta = popularity >= 99.99 and -0.01 or 0.01
	passed = testSet(
		file,
		"country.set_ideology_popularity",
		{ tag = context.playerTag, ideology = rulingIdeologyName },
		popularityReader
	) and passed
	passed = testAddAndRestore(
		file,
		"country.add_ideology_popularity",
		{ tag = context.playerTag, ideology = rulingIdeologyName },
		popularityReader,
		popularityDelta
	) and passed

	local organizationReader = function()
		return country:AccessIdeologyOrganization()
			:GetValue(country:GetRulingIdeology()):Get()
	end
	local organization = organizationReader()
	local organizationDelta = organization >= 99.99 and -0.01 or 0.01
	passed = testSet(
		file,
		"country.set_ideology_organization",
		{ tag = context.playerTag, ideology = rulingIdeologyName },
		organizationReader
	) and passed
	passed = testAddAndRestore(
		file,
		"country.add_ideology_organization",
		{ tag = context.playerTag, ideology = rulingIdeologyName },
		organizationReader,
		organizationDelta
	) and passed

	local targetTagName = context.playerTag == "JAP" and "CHI" or "JAP"
	local targetTag = CCountryDataBase.GetTag(targetTagName)
	local relationReader = function()
		return country:GetRelation(targetTag):GetValue():Get()
	end
	passed = testSet(
		file,
		"diplomacy.set_relation",
		{
			tag = context.playerTag,
			target_tag = targetTagName
		},
		relationReader
	) and passed
	passed = testAddAndRestore(
		file,
		"diplomacy.add_relation",
		{
			tag = context.playerTag,
			target_tag = targetTagName
		},
		relationReader,
		0.01
	) and passed

	local threatReader = function()
		return country:GetRelation(targetTag):GetThreat():Get()
	end
	passed = testSet(
		file,
		"diplomacy.set_threat",
		{
			tag = context.playerTag,
			target_tag = targetTagName
		},
		threatReader
	) and passed
	passed = testAddAndRestore(
		file,
		"diplomacy.add_threat",
		{
			tag = context.playerTag,
			target_tag = targetTagName
		},
		threatReader,
		0.01
	) and passed

	passed = testSet(
		file,
		"espionage.set_presence_level",
		{
			tag = context.playerTag,
			target_tag = targetTagName
		},
		function()
			return country:GetSpyPresence(targetTag):GetLevel():Get()
		end
	) and passed

	local capital = country:GetCapitalLocation()
	passed = testSet(
		file,
		"intelligence.set_province_level",
		{
			tag = context.playerTag,
			province_id = capital:GetProvinceID()
		},
		function()
			return capital:GetIntelLevel(context.playerTagObject)
		end
	) and passed

	local modifier = "china_war_emergency_local_mobilization"
	local modifierAdded = execute(
		file,
		"country.add_modifier",
		{
			tag = context.playerTag,
			modifier = modifier,
			duration_days = 1
		},
		".modifier_add"
	)
	local modifierRemoved = modifierAdded and execute(
		file,
		"country.remove_modifier",
		{ tag = context.playerTag, modifier = modifier },
		".modifier_remove"
	)
	passed = modifierAdded and modifierRemoved and passed
	appendLine(file, "RESULT passed=" .. tostring(passed))
	file:close()
	os.rename(RunningPath, CompletedPath)
	P.Completed = true
	return true
end

function P.ShouldTick(context)
	return context
		and context.playerTag ~= ""
		and fileExists(RequestPath)
end

function P.Tick(context)
	local success, result = pcall(runProbe, context)
	if success then
		return result
	end
	local file = io.open(ReportPath, "a")
	if file then
		appendLine(file, "PROBE_ERROR message=" .. tostring(result))
		file:close()
	end
	os.rename(RunningPath, CompletedPath)
	P.Completed = true
	return false
end

return P
