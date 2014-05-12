#!../luajit

package.cpath = "/home/essele/dev/unifi/c/?.so"

unisvr = package.loadlib("./unisvr.so", "luaopen_unisvr")()


--[[
  [[ Generate a string of random hex bytes up to a specific
  [[ length
  [[
  ]]--

function hex_string(len)
	local i = 0
	local rc = ""

	math.randomseed(unisvr.time());
	while(i < len) do
		rc = rc .. string.format("%02x", math.random(0,255));
		i = i +1
	end
	return rc
end

print(hex_string(16));




unisvr.init()

x = unisvr.get_client()

if(x.encrypted) then
	rc, err = unisvr.decrypt(x, "ba86f2bbe107c7c57eb5f2690775c712")
	if(not rc) then
		print("err is "..err);
		return 0
	end
end

print("t is " .. tostring(x))

t = {}
t._type = "setparam"
t.server_time_in_utc = unisvr.time()

unisvr.add_cfg(t, "mgmtcfg", "mgmt.is_default", "false");
unisvr.add_cfg(t, "mgmtcfg", "mgmt.authkey", "TODO");
unisvr.add_cfg(t, "mgmtcfg", "mgmt.cfgversion", "TODO");
unisvr.add_cfg(t, "mgmtcfg", "mgmt.servers.1.url", "TODO");
unisvr.add_cfg(t, "mgmtcfg", "mgmt.selfrun_guest_mode", "TODO");
unisvr.add_cfg(t, "mgmtcfg", "selfrun_guest_mode", "TODO");
unisvr.add_cfg(t, "mgmtcfg", "cfgversion", "TODO");
unisvr.add_cfg(t, "mgmtcfg", "led_enabled", "TODO");
unisvr.add_cfg(t, "mgmtcfg", "authkey", "TODO");

-- If authkey was provided, then we don't need the standalone "authkey" bit


print(unisvr.serialise(t))



--print(unisvr.serialise(x))

-- print("rrr is " .. t.rrr)
-- print("rrr is " .. type(t.rrr))

--[[
t = {}
t.fred = 1
t.bill = "new"
t.three = {}

t.three.__list = 1
t.three[1] = "hello"
t.three[2] = "blah"
t.three[3] = 556677.45

t.four = {}
t.four.athstats = {}
t.four.athstats.__list = 1
t.four.athstats[1] = {}
t.four.athstats[1].ast_ath_reset = 1
t.four.athstats[1].n_rx_affr = 1
t.four.athstats[1].n_tx_pkts = 0
t.four.athstats[2] = {}
t.four.athstats[2].ast_ath_reset = 1
t.four.athstats[2].n_rx_affr = 1
t.four.athstats[2].n_tx_pkts = 0
t.four.a = 45;
t.four.b = 44;

print(unisvr.serialise(t))
]]--

