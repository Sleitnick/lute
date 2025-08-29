export type Socket = {}

local socket = {}

function socket.connect(addr: string, port: number): Socket
	error("not yet implemented")
end

function socket.close(sock: Socket)
	error("not yet implemented")
end

function socket.send(sock: Socket, data: buffer, offset: number?, length: number?)
	error("not yet implemented")
end

function socket.recv(sock: Socket, data: buffer): number
	error("not yet implemented")
end

return socket
