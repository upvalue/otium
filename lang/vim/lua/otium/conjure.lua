local client = require("conjure.client")
local config = require("conjure.config")
local extract = require("conjure.extract")
local log = require("conjure.log")
local mapping = require("conjure.mapping")
local stdio = require("conjure.remote.stdio")

local M = {
  ["buf-suffix"] = ".ot",
  ["comment-prefix"] = "; ",
}

local request_end = string.char(31)
local response_end = string.char(30) .. "ot> "

config.merge({
  client = {
    otium = {
      stdio = {
        command = { "otium", "--server" },
        prompt_pattern = response_end,
        mapping = {
          start = "cs",
          stop = "cS",
          interrupt = "ei",
          macroexpand = "mx",
        },
      },
    },
  },
})

local function cfg(keys)
  local path = { "client", "otium", "stdio" }
  vim.list_extend(path, keys)
  return config["get-in"](path)
end

local state = client["new-state"](function()
  return { repl = nil }
end)

local function display_status(status)
  log.append({ M["comment-prefix"] .. vim.inspect(cfg({ "command" })) .. " (" .. status .. ")" }, {
    ["break?"] = true,
  })
end

local function response_lines(messages)
  local chunks = {}
  for _, message in ipairs(messages) do
    table.insert(chunks, message.out or message.err or "")
  end

  local lines = {}
  local response = table.concat(chunks):gsub("\r", "")
  for line in (response .. "\n"):gmatch("(.-)\n") do
    if line ~= "" then
      table.insert(lines, line)
    end
  end
  return lines
end

local function with_repl(callback)
  local repl = state().repl
  if repl then
    return callback(repl)
  end
  log.append({ M["comment-prefix"] .. "No Otium server running" })
end

M["eval-str"] = function(opts)
  return with_repl(function(repl)
    repl.send(opts.code .. "\n" .. request_end .. "\n", function(messages)
      local lines = response_lines(messages)
      if opts["on-result"] then
        opts["on-result"](lines[#lines] or "")
      end
      log.append(lines)
    end, { ["batch?"] = true })
  end)
end

M["eval-file"] = function(opts)
  local code = table.concat(vim.fn.readfile(opts["file-path"]), "\n")
  return M["eval-str"](vim.tbl_extend("force", opts, { code = code }))
end

M["doc-str"] = function(opts)
  local code = "(describe '" .. opts.code .. ")"
  return M["eval-str"](vim.tbl_extend("force", opts, { code = code }))
end

function M.macroexpand()
  local form = extract.form({})
  if not form then
    return
  end
  return M["eval-str"]({
    action = "eval",
    code = "(macroexpand '" .. form.content .. ")",
    origin = "macroexpand",
    range = form.range,
  })
end

function M.stop()
  local repl = state().repl
  if repl then
    repl.destroy()
    state().repl = nil
    display_status("stopped")
  end
end

function M.start()
  if state().repl then
    log.append({
      M["comment-prefix"] .. "Otium server is already running",
      M["comment-prefix"] .. "Stop it with " .. config["get-in"]({ "mapping", "prefix" }) .. cfg({ "mapping", "stop" }),
    }, { ["break?"] = true })
    return
  end

  state().repl = stdio.start({
    cmd = cfg({ "command" }),
    ["prompt-pattern"] = cfg({ "prompt_pattern" }),
    ["on-success"] = function()
      display_status("started")
    end,
    ["on-error"] = function(err)
      display_status(tostring(err))
    end,
    ["on-exit"] = function(code, signal)
      state().repl = nil
      if type(code) == "number" and code > 0 then
        log.append({ M["comment-prefix"] .. "process exited with code " .. code })
      end
      if type(signal) == "number" and signal > 0 then
        log.append({ M["comment-prefix"] .. "process exited with signal " .. signal })
      end
      display_status("stopped")
    end,
    ["on-stray-output"] = function(message)
      log.append(response_lines({ message }))
    end,
  })
end

function M.interrupt()
  return with_repl(function(repl)
    log.append({ M["comment-prefix"] .. "Sending interrupt signal" }, { ["break?"] = true })
    repl["send-signal"]("sigint")
  end)
end

M["on-filetype"] = function()
  mapping.buf("OtiumStart", cfg({ "mapping", "start" }), M.start, { desc = "Start the Otium server" })
  mapping.buf("OtiumStop", cfg({ "mapping", "stop" }), M.stop, { desc = "Stop the Otium server" })
  mapping.buf("OtiumInterrupt", cfg({ "mapping", "interrupt" }), M.interrupt, { desc = "Interrupt Otium evaluation" })
  mapping.buf(
    "OtiumMacroexpand",
    cfg({ "mapping", "macroexpand" }),
    M.macroexpand,
    { desc = "Macroexpand the current form" }
  )
end

M["on-load"] = M.start
M["on-exit"] = M.stop

return M
