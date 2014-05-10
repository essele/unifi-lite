#!./luajit

package.cpath = "/home/essele/dev/unifi/c/?.so"

unisvr = package.loadlib("c/unisvr.so", "luaopen_unisvr")()

local ffi = require("ffi")
ffi.cdef[[
	int socket (int __domain, int __type, int __protocol);

	struct sockaddr_in {
		unsigned short int		sin_family;
		unsigned short int		sin_port;
		unsigned int			sin_addr;
		char					padding[8];
	};

	int bind(int __fd, struct sockaddr_in *__addr, size_t __len);

]]

AF_INET = 2
SOCK_STREAM = 1

fd = ffi.C.socket(AF_INET, 2, 0);

svr = ffi.new("struct sockaddr_in");
svr.sin_family = AF_INET;
svr.sin_port = 2345;
svr.sin_addr = 0x00000000;

size = ffi.sizeof(svr);

rc = ffi.C.bind(fd, svr, 16);

io.write("Socket" .. fd);

unisvr.unser()

--[[
unisvr.init()

t = unisvr.get_client();

print("Magic: " .. t.magic);
print("IV: " .. t.iv);
print("Data Length: " .. t.datalength);
print("Compressed: " .. t.compressed);
print("Encrypted: " .. t.encrypted);

x = unisvr.decrypt(t);

--]]
