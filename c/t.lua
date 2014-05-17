#!../luajit

package.cpath = "/home/essele/dev/unifi/c/?.so"

unisvr = package.loadlib("./unisvr.so", "luaopen_unisvr")()



print(unisvr.gen_hex(16));

x = unisvr.read_table("fred");
print("x="..tostring(x));
print("a="..x.a);
print("c="..x.c);

--l = {}
--l.a = 45;
--l.b = 98;
--l.c = "hello";
--unisvr.write_table("fred", l)



-- we will keep live data in a table (key=mac) so that we don't
-- write too much data to the flash, we don't need to persist
-- too much.
registry = {}
registry.settings = {}			-- overall settings
registry.ap = {}				-- access points

-- the key to use when we get an initial request

default_key = "ba86f2bbe107c7c57eb5f2690775c712"


-- prepare the initial response for new clients
function handle_adopting(c)
	print("IN ADOPING NOW")
	print(c.data.model)
	print(c.data.model_display)
	if(c.data.cfgversion == "?") then
		print("New Version Needed")
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

		c.data = t;
		print("Enc: v="..tostring(v).."a="..tostring(a))

		-- If authkey was provided, then we don't need the standalone "authkey" bit
		print(unisvr.serialise(t))
	end
end

-- there is a basic state machine process that we need to follow
-- for "inform" messages.
--
-- If we don't know the client then we put it as pending (PENDING)
-- Once set to adopt, we generate keys and send the base reply (ADOPTING)
-- Once the base reply is fed back we configure (PROVISIONING)
-- When complete, it's (CONNECTED)

function process_inform(c)
	local auth_key
	print("Processing client: "..c.mac)

	-- first we work out if we can decrypt the message if it's
	-- encrypted
	
	if(c.encrypted) then
		if(registry.ap[c.mac] and registry.ap[c.mac].auth_key) then
			auth_key = registry.ap[c.mac].auth_key
			local rc, err = unisvr.decrypt(c, auth_key)
			if(not rc) then
				print("failed to decrypt using specified key, will try default")
			end
		end

		if(not c.data) then
			auth_key = "ba86f2bbe107c7c57eb5f2690775c712"	
			local rc, err = unisvr.decrypt(c, auth_key)
			if(not rc) then
				print("err is "..err);
				return 0
			end
		end
	end

	if(not registry.ap[c.mac]) then
		-- this is the completely unknown case, we simply mark
		-- the client as pending and drop through...
		registry.ap[c.mac] = {}
		registry.ap[c.mac].state = "PENDING"
		registry.ap[c.mac].inform = c.data
		print("new client in pending state: "..c.mac)
	end

	-- We know the client by this stage, but it could just be pending
	-- in which case we reply with a 400 Bad Request
	if(registry.ap[c.mac].state == "PENDING") then
		c.status = 400
		c.data = nil
	-- FAKE FAKE FAKE
		registry.ap[c.mac].state = "ADOPTING"
		unisvr.reply(c)
		return
	end

	-- If we have a client in "ADOPTING" then we need to see what
	-- kind of request it was. If we have no config or the wrong
	-- config then we set some defaults and reply. Otherwise we
	-- start provisioning
	if(registry.ap[c.mac].state == "ADOPTING") then
		handle_adopting(c)
		c.status = 200;

		-- if we are encrypted then we need to encrypt the data
		-- with the key we used to decrypt (not the new key)
		if(c.encrypted) then
			local rc, err = unisvr.encrypt(c, auth_key);
			if(not rc) then
				print("err is "..err);
				return 0
			end
		end
		unisvr.reply(c)
		return
	end

end


function process_admin(c)
	print("have admin message");
	if(not c.data) then
		print("malformed request")
		c.data = {}
		c.data.error = "malformed request"
		unisvr.reply(c);
		return 0
	end

	print("action is "..(c.data.action))
end


-- this is the main loop that sits waiting for network connections.
-- we will accept an "inform" message from an access point or a local
-- connection from the admin client

local c			-- the client/control message

unisvr.init()
while(1) do
	-- get an "inform" or a "control" message
	c = unisvr.get_client()

	-- process the message
	if(c.inform) then
		process_inform(c)
	elseif(c.admin) then
		process_admin(c)
	else
		print("unknown message")
	end
end

-- read our record...
-- if we don't have one then create some stuff


if(c.encrypted) then
	local rc, err = unisvr.decrypt(c, "ba86f2bbe107c7c57eb5f2690775c712")
	if(not rc) then
		print("err is "..err);
		return 0
	end
end

print(unisvr.serialise(c))


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

