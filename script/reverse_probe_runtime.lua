local P = {}

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
local RequestPath = Root .. "\\new_core\\reverse_probe_runtime.request"
local RunningPath = RequestPath .. ".running"
local CompletedPath = RequestPath .. ".completed"
local LogPath = Root .. "\\new_core\\reverse_probe_runtime.log"

local function fileExists(path)
	local file = io.open(path, "r")
	if not file then
		return false
	end
	file:close()
	return true
end

local function trim(value)
	return (value:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function readProbeIds(path)
	local file = io.open(path, "r")
	if not file then
		return nil, "reverse_probe_request_open_failed"
	end
	local ids = {}
	local all = false
	for rawLine in file:lines() do
		local line = trim(rawLine:gsub("#.*", ""))
		if line ~= "" then
			if string.lower(line) == "all" then
				all = true
			else
				table.insert(ids, line)
			end
		end
	end
	file:close()
	if all then
		return {}, nil
	end
	if #ids == 0 then
		return nil, "reverse_probe_request_empty"
	end
	return ids, nil
end

local function appendLine(file, text)
	file:write(tostring(text), "\n")
	file:flush()
end

local function finishRequest()
	os.remove(CompletedPath)
	os.rename(RunningPath, CompletedPath)
end

local function runProbe()
	os.remove(RunningPath)
	if not os.rename(RequestPath, RunningPath) then
		return false
	end
	local ids, requestError = readProbeIds(RunningPath)
	local log = io.open(LogPath, "a")
	if not log then
		finishRequest()
		return false
	end
	if not ids then
		appendLine(log, "REQUEST_ERROR message=" .. tostring(requestError))
		log:close()
		finishRequest()
		return false
	end
	if not NewCoreNative
		or type(NewCoreNative.RunReverseProbes) ~= "function" then
		appendLine(log, "RUN_ERROR message=reverse_probe_bridge_unavailable")
		log:close()
		finishRequest()
		return false
	end

	local callOk, passed, report, code, message = pcall(
		NewCoreNative.RunReverseProbes,
		ids
	)
	if not callOk then
		appendLine(log, "RUN_ERROR message=" .. tostring(passed))
		log:close()
		finishRequest()
		return false
	end
	appendLine(
		log,
		string.format(
			"RUN id=%s passed=%s code=%s generation=%s barrier=%s player=%s message=%s",
			tostring(report and report.run_id or 0),
			tostring(passed),
			tostring(code or ""),
			tostring(report and report.lifecycle_generation or 0),
			tostring(report and report.barrier_generation or 0),
			tostring(report and report.player_tag or ""),
			tostring(message or "")
		)
	)
	if report and report.results then
		for probeId, result in pairs(report.results) do
			appendLine(
				log,
				string.format(
					"RESULT id=%s status=%s evidence=%s duration_us=%s message=%s",
					tostring(probeId),
					tostring(result.status or ""),
					tostring(result.evidence or ""),
					tostring(result.duration_us or 0),
					tostring(result.message or "")
				)
			)
		end
	end
	log:close()
	finishRequest()
	return passed == true
end

function P.ShouldTick(context)
	return context
		and context.playerTag ~= ""
		and fileExists(RequestPath)
end

function P.Tick()
	return runProbe()
end

return P
