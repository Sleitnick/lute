#include "lute/socket.h"

#include <string>
#include <utility>
#include <vector>

#include "uv.h"
#include "lute/runtime.h"
#include "lute/userdatas.h"

namespace sock
{

struct Socket
{
	uv_tcp_t* sock;
	bool open;
};

int socket_connect(lua_State* L)
{
	const char* addr = luaL_checkstring(L, 1);
	const int port = luaL_checkinteger(L, 2);
	luaL_argcheck(L, port >= 0 && port <= 65535, 2, "port out of range");

	uv_tcp_t* socket = new uv_tcp_t();
	int sock_init_err = uv_tcp_init(uv_default_loop(), socket);
    if (sock_init_err)
    {
        luaL_errorL(L, "%s", uv_strerror(sock_init_err));
    }

	uv_connect_t* connect = new uv_connect_t();
    connect->data = new ResumeToken(getResumeToken(L));

	sockaddr_in dest;
	int ipv4_err = uv_ip4_addr(addr, port, &dest);
    if (ipv4_err)
    {
        luaL_errorL(L, "%s", uv_strerror(ipv4_err));
    }

	int connect_err = uv_tcp_connect(connect, socket, reinterpret_cast<const sockaddr*>(&dest), [](uv_connect_t *req, int status)
	{
		ResumeToken* token = reinterpret_cast<ResumeToken*>(req->data);
		if (status < 0)
		{
			token->get()->fail(uv_strerror(status));
			delete token;
			delete req;
			return;
		}

		token->get()->complete(
			[data = std::move(reinterpret_cast<uv_tcp_t*>(req->handle))](lua_State* L)
			{
				Socket* sock_ud = static_cast<Socket*>(lua_newuserdatatagged(L, sizeof(Socket), kSocketTag));
				sock_ud->sock = data;
				sock_ud->open = true;
				return 1;
			}
		);
			
		delete token;
		delete req;
	});

	if (connect_err)
	{
		// TODO: What do I do with the resumption token???
		delete connect;
		luaL_errorL(L, "%s", uv_strerror(connect_err));
	}

	return lua_yield(L, 0);
}

int socket_close(lua_State* L)
{
	Socket* sock = static_cast<Socket*>(lua_touserdatatagged(L, 1, kSocketTag));
	luaL_argcheck(L, sock, 1, "expected socket");

	if (!sock->open)
	{
		return 0;
	}

    sock->sock->data = new ResumeToken(getResumeToken(L));
	sock->open = false;

	uv_close(
		(uv_handle_t*)sock->sock,
		[](uv_handle_t *handle)
		{
			ResumeToken* token = reinterpret_cast<ResumeToken*>(handle->data);
			token->get()->complete(
				[](lua_State* L)
				{
					return 0;
				}
			);
				
			delete token;
			// TODO: Do I need to delete "handle" here?
		}
	);

	return lua_yield(L, 0);
}

int socket_send(lua_State* L)
{
	Socket* sock = static_cast<Socket*>(lua_touserdatatagged(L, 1, kSocketTag));
	luaL_argcheck(L, sock, 1, "expected socket");

	size_t buf_len;
	void* buf = luaL_checkbuffer(L, 2, &buf_len);

	int buffer_offset = luaL_optinteger(L, 3, -1);
	int buffer_len = luaL_optinteger(L, 4, -1);

	if (buffer_offset == -1) {
		buffer_offset = 0;
		buffer_len = -1;
	}
	if (buffer_len == -1) {
		buffer_len = buf_len;
	}

	luaL_argcheck(L, buffer_offset >= 0 && buffer_offset <= (static_cast<int>(buf_len) - 1), 3, "buffer offset out of range");
	luaL_argcheck(L, buffer_offset + buffer_len <= static_cast<int>(buf_len), 4, "buffer length out of range");

	uv_write_t* req = new uv_write_t();
    req->data = new ResumeToken(getResumeToken(L));

	uv_buf_t req_buf = uv_buf_init(&static_cast<char*>(buf)[buffer_offset], buffer_len);

	int write_err = uv_write(
		req,
		reinterpret_cast<uv_stream_t*>(sock->sock),
		&req_buf,
		1,
		[](uv_write_t *req, int status)
		{
			ResumeToken* token = reinterpret_cast<ResumeToken*>(req->data);
			if (status < 0)
			{
				token->get()->fail(uv_strerror(status));
				delete token;
				delete req;
				return;
			}

			token->get()->complete(
				[](lua_State* L)
				{
					return 0;
				}
			);

			delete token;
			delete req;
		}
	);

	if (write_err)
	{
		// TODO: What do I do with the resumption token???
		delete req;
		luaL_errorL(L, "%s", uv_strerror(write_err));
	}

	return lua_yield(L, 0);
}

struct ResumeCaptureInformation
{
    explicit ResumeCaptureInformation(lua_State* L, void* buf, size_t buf_len)
        : token(getResumeToken(L)), buf(buf), buf_len(buf_len)
    {
    }

    ResumeToken token = nullptr;
	void* buf = nullptr;
	size_t buf_len = 0;
	size_t offset = 0;
};

int socket_recv(lua_State* L)
{
	Socket* sock = static_cast<Socket*>(lua_touserdatatagged(L, 1, kSocketTag));
	luaL_argcheck(L, sock, 1, "expected socket");

	size_t alloc_buf_len;
	void* alloc_buf = luaL_checkbuffer(L, 2, &alloc_buf_len);
	
	ResumeCaptureInformation* token_info = new ResumeCaptureInformation(L, alloc_buf, alloc_buf_len);
	sock->sock->data = token_info;

	int read_start_err = uv_read_start(
		reinterpret_cast<uv_stream_t*>(sock->sock),
		// alloc buffer:
		[](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
		{
			ResumeCaptureInformation* info = reinterpret_cast<ResumeCaptureInformation*>(handle->data);
			buf->base = &static_cast<char*>(info->buf)[info->offset];
			buf->len = info->buf_len - info->offset;
		},
		// on read:
		[](uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
		{
			ResumeCaptureInformation* info = reinterpret_cast<ResumeCaptureInformation*>(stream->data);

			if (nread > 0)
			{
				info->offset += nread;
				if (info->offset == info->buf_len)
				{
					uv_read_stop(stream);
					info->token->complete(
						[data = std::move(info->buf_len)](lua_State* L)
						{
							lua_pushinteger(L, data);
							return 1;
						}
					);
					delete info;
				}
			}
			else if (nread < 0)
			{
				uv_read_stop(stream);
				info->token->fail("failed to read");
				delete info;
			}
		}
	);

	if (read_start_err)
	{
		// TODO: What do I do with the resumption token???
		luaL_errorL(L, "%s", uv_strerror(read_start_err));
	}

	return lua_yield(L, 0);
}

}

int luaopen_socket(lua_State* L)
{
    luaL_register(L, "socket", sock::lib);

    return 1;
}

int luteopen_socket(lua_State* L)
{
    lua_createtable(L, 0, std::size(sock::lib));

    for (auto& [name, func] : sock::lib)
    {
        if (!name || !func)
            break;

        lua_pushcfunction(L, func, name);
        lua_setfield(L, -2, name);
    }

    lua_setreadonly(L, -1, 1);

	lua_setuserdatadtor(
		L,
		kSocketTag,
		[](lua_State* L, void* ptr)
		{
			sock::Socket* s = static_cast<sock::Socket*>(ptr);
			if (s->open)
			{
				s->open = false;
				uv_close((uv_handle_t*)s->sock, [](uv_handle_t *handle){});
			}
		}
	);

    return 1;
}
