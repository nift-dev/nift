lib := ""
if(os() == "windows") { lib = ffi_open("ws2_32.dll") }
else if(os() == "macos") { lib = ffi_open("libSystem.B.dylib") }
else { lib = ffi_open("libc.so.6") }
fails := 0
fail := (msg) => { print("FAIL " + msg); fails += 1 }
zeros := (n) => { a := []; i := 0; while(i < n) { a.push(0); i += 1 } return bytes(a) }
sockaddr4 := (port) => {
    hi := ((port - (port % 256)) / 256).to_int()
    return bytes([2,0, hi, (port % 256), 127,0,0,1, 0,0,0,0,0,0,0,0])
}
af_inet := 2
sock_stream := 1
if(os() == "windows") {
    wsa := ffi_buffer(zeros(512))
    ws := ffi_call(lib, "WSAStartup", "i32(i32,buffer)", 514, wsa)
    if(ws != 0) { fail("WSAStartup") }
}
listener := ffi_call(lib, "socket", "i64(i32,i32,i32)", af_inet, sock_stream, 0)
if(listener < 0) { fail("socket") }
sa := ffi_buffer(sockaddr4(0))
b := ffi_call(lib, "bind", "i32(i64,buffer,i64)", listener, sa, 16)
if(b != 0) { fail("bind=" + b.to_string()) }
nb := ffi_buffer(zeros(16))
sl := ffi_buffer(bytes([16,0,0,0,0,0,0,0]))
ffi_call(lib, "getsockname", "i32(i64,buffer,buffer)", listener, nb, sl)
na := ffi_bytes(nb)
port := na[2].to_int() * 256 + na[3].to_int()
l := ffi_call(lib, "listen", "i32(i64,i32)", listener, 4)
if(l != 0) { fail("listen") }
client := ffi_call(lib, "socket", "i64(i32,i32,i32)", af_inet, sock_stream, 0)
cb := ffi_buffer(sockaddr4(port))
c := ffi_call(lib, "connect", "i32(i64,buffer,i64)", client, cb, 16)
if(c != 0) { fail("connect") }
ab := ffi_buffer(zeros(16))
al := ffi_buffer(bytes([16,0,0,0,0,0,0,0]))
accepted := ffi_call(lib, "accept", "i64(i64,buffer,buffer)", listener, ab, al)
if(accepted < 0) { fail("accept") }
payload := bytes([72,105,33])
pb := ffi_buffer(payload)
s := ffi_call(lib, "send", "i64(i64,buffer,i64,i32)", client, pb, 3, 0)
if(s != 3) { fail("send=" + s.to_string()) }
rb := ffi_buffer(zeros(16))
r := ffi_call(lib, "recv", "i64(i64,buffer,i64,i32)", accepted, rb, 16, 0)
rd := ffi_bytes(rb)
if(r != 3 || rd[0] != 72 || rd[1] != 105 || rd[2] != 33) { fail("recv roundtrip") }
if(os() == "windows") { ffi_call(lib, "closesocket", "i32(i64)", accepted); ffi_call(lib, "closesocket", "i32(i64)", client); ffi_call(lib, "closesocket", "i32(i64)", listener); ffi_call(lib, "WSACleanup", "i32()") }
else { ffi_call(lib, "close", "i32(i64)", accepted); ffi_call(lib, "close", "i32(i64)", client); ffi_call(lib, "close", "i32(i64)", listener) }
print("PASS socket proof (port " + port.to_string() + ")")
if(fails > 0) { print("FAILED " + fails.to_string()) }
