#!../luajit

package.cpath = "/home/essele/dev/unifi/c/?.so"

unisvr = package.loadlib("./unisvr.so", "luaopen_unisvr")()

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

print(unisvr.serialise(x))

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

