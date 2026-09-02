local M = {}

local function getNative()
	local native = rawget(_G, "NewCoreNative")
	if type(native) ~= "table" then
		return nil
	end
	return native
end

function M.IsAvailable(operation)
	local native = getNative()
	return native ~= nil
		and type(native.HasEffect) == "function"
		and native.HasEffect(operation) == true
end

function M.Execute(batch)
	local native = getNative()
	if not native or type(native.ExecuteEffects) ~= "function" then
		return false,
			"native_effect_bridge_unavailable",
			"NewCoreNative.ExecuteEffects is unavailable",
			0
	end

	local called, success, code, message, transactionId = pcall(
		native.ExecuteEffects,
		batch
	)
	if not called then
		return false,
			"native_effect_bridge_exception",
			tostring(success),
			0
	end
	return success == true,
		code or "native_effect_unknown",
		message or "",
		tonumber(transactionId) or 0
end

function M.ExecuteTransaction(source, effects, atomic)
	return M.Execute({
		source = source or "lua",
		atomic = atomic ~= false,
		effects = effects or {}
	})
end

function M.ExecuteEffect(operation, arguments, source)
	return M.ExecuteTransaction(
		source,
		{
			{
				operation = operation,
				arguments = arguments or {}
			}
		},
		true
	)
end

return M
