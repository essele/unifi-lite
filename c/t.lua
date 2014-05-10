#!../luajit

package.cpath = "/home/essele/dev/unifi/c/?.so"

unisvr = package.loadlib("./unisvr.so", "luaopen_unisvr")()

unisvr.init()

x = unisvr.get_client()

t = unisvr.decrypt(x)

print("t is " .. tostring(t))

print("rrr is " .. t.rrr)
print("rrr is " .. type(t.rrr))
