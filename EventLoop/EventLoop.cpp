#include "EventLoop.hpp"

#include <WS2tcpip.h>
#include <MSWSock.h>

#pragma comment(lib, "ws2_32.lib")


struct wsa_initer
{
	WSADATA wsd{ 0 };
	int err = 0;

	wsa_initer()
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0)
			err = WSAGetLastError();
	}

	~wsa_initer()
	{
		if (err != 0)
			WSACleanup();
	}

	static void init()
	{
		static wsa_initer instance;
	}
};

struct comp_port
{
	HANDLE val = nullptr;
	int err = ERROR_SUCCESS;

	comp_port()
	{
		this->val = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

		if (this->val == nullptr)
			this->err = GetLastError();
	}

	~comp_port()
	{
		if (err != ERROR_SUCCESS)
			CloseHandle(this->val);
	}

	static comp_port &get_singleton()
	{
		static comp_port instance;
		return instance;
	}
};

struct winsock_ex
{
	LPFN_ACCEPTEX accept_ex = nullptr;
	int err = 0;

	winsock_ex()
	{
		SOCKET sock = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);

		GUID acceptex_guid = WSAID_ACCEPTEX;
		DWORD bytes_returned = 0;

		if (WSAIoctl(sock,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&acceptex_guid, sizeof(acceptex_guid),
			&this->accept_ex, sizeof(this->accept_ex),
			&bytes_returned, NULL, NULL) != 0)
		{
			this->err = WSAGetLastError();
			return;
		}

		closesocket(sock);
	}

	static winsock_ex &get_singleton()
	{
		static winsock_ex initer;
		return initer;
	}
};

union sockaddr_t {
	struct sockaddr         sa;
	struct sockaddr_in      s4;
	struct sockaddr_in6     s6;
	struct sockaddr_storage ss;
};


enum class overlapped_type
{
	o_accept,
	o_connect,
	o_recv,
	o_send,
};

struct overlapped_t
{
	OVERLAPPED base{ 0 };
	overlapped_type type;

	overlapped_t() = delete;
	overlapped_t(overlapped_type typ) : type(typ) {}
};

struct evl::__internal::socket_t
{
	SOCKET sock = INVALID_SOCKET;
	sockaddr_t addr{ 0 };

	HANDLE comp_port = nullptr;
	socket_t *parent = nullptr;

	overlapped_t ovlp_init;
	overlapped_t ovlp_recv{ overlapped_type::o_recv };
	overlapped_t ovlp_send{ overlapped_type::o_send };

	std::weak_ptr<vinit_task_base> _vt_init;
	std::weak_ptr<vrecv_task> _vt_recv;
	std::weak_ptr<vsend_task> _vt_send;

	~socket_t()
	{
		closesocket(this->sock);
	}

	static socket_t *from(overlapped_t *ovlp)
	{
		switch (ovlp->type)
		{
			case overlapped_type::o_accept: return reinterpret_cast<socket_t *>(reinterpret_cast<char *>(ovlp) - offsetof(socket_t, ovlp_init));
			case overlapped_type::o_connect: return reinterpret_cast<socket_t *>(reinterpret_cast<char *>(ovlp) - offsetof(socket_t, ovlp_init));
			case overlapped_type::o_recv: return reinterpret_cast<socket_t *>(reinterpret_cast<char *>(ovlp) - offsetof(socket_t, ovlp_recv));
			case overlapped_type::o_send: return reinterpret_cast<socket_t *>(reinterpret_cast<char *>(ovlp) - offsetof(socket_t, ovlp_send));
		}
	}

	socket_t(overlapped_type typ) :
		ovlp_init(typ)
	{
	}

	void recv_async(WSABUF *wsa_buf)
	{
		DWORD bytes_recv = 0;
		DWORD flags = MSG_PUSH_IMMEDIATE;
		if (WSARecv(this->sock, wsa_buf, 1, &bytes_recv, &flags, &this->ovlp_recv.base, nullptr) == SOCKET_ERROR)
		{
			if (WSAGetLastError() != WSA_IO_PENDING)
				throw 1;
		}
	}

	void send_async(WSABUF *wsa_buf)
	{
		DWORD bytes_send = 0;
		DWORD flags = 0;
		if (WSASend(this->sock, wsa_buf, 1, &bytes_send, flags, &this->ovlp_send.base, nullptr) == SOCKET_ERROR)
		{
			if (WSAGetLastError() != WSA_IO_PENDING)
				throw 1;
		}
	}

	[[nodiscard]]
	static std::unique_ptr<socket_t> create_listen(HANDLE comp_port)
	{
		std::unique_ptr<socket_t> result = std::make_unique<socket_t>(overlapped_type::o_accept);
		result->comp_port = comp_port;

		result->sock = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (result->sock == INVALID_SOCKET)
			throw 1;

		if (CreateIoCompletionPort((HANDLE)result->sock, result->comp_port, 0, 0) == nullptr)
			throw 1;

		result->addr.s4.sin_family = AF_INET;
		result->addr.s4.sin_addr.S_un.S_addr = INADDR_ANY;
		result->addr.s4.sin_port = htons(5150);

		if (bind(result->sock, &result->addr.sa, sizeof(result->addr.s4)) != 0)
			throw 1;

		if (listen(result->sock, SOMAXCONN) != 0)
			throw 1;

		return result;
	}

	std::unique_ptr<socket_t> prepare_accept(char *buf)
	{
		std::unique_ptr<socket_t> client = std::make_unique<socket_t>(overlapped_type::o_accept);
		client->comp_port = this->comp_port;
		client->parent = this;

		client->sock = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (client->sock == INVALID_SOCKET)
			throw 1;

		DWORD breceived = 0;

		if (winsock_ex::get_singleton().accept_ex(this->sock, client->sock, buf, 0, sizeof(sockaddr_t::s4) + 16, sizeof(sockaddr_t::s4) + 16, &breceived, &client->ovlp_init.base) == FALSE)
		{
			if (WSAGetLastError() != ERROR_IO_PENDING)
				throw 1;
		}

		if (CreateIoCompletionPort((HANDLE)client->sock, this->comp_port, 0, 0) == nullptr)
			throw 1;

		return client;
	}
};

struct evl::__internal::vrecv_task : evl::__internal::vtask_base
{
	context *_ctx = nullptr;
	std::vector<char> _value;
	WSABUF _wsa_buf{ 0 };
	std::weak_ptr<vtask_base> _parent_vt;

	virtual void resume() override
	{
		this->_ctx->_add_ready_task(_parent_vt.lock());
	}
};

struct evl::__internal::vsend_task : evl::__internal::vtask_base
{
	context *_ctx = nullptr;
	std::vector<char> _value;
	WSABUF _wsa_buf{ 0 };
	std::weak_ptr<vtask_base> _parent_vt;

	virtual void resume() override
	{
		this->_ctx->_add_ready_task(_parent_vt.lock());
	}
};

struct evl::__internal::vaccept_task : evl::__internal::vinit_task_base
{
	std::weak_ptr<vtask_base> _parent_vt;
	tcp_client _value;
	std::vector<char> _accept_buffer;

	virtual void resume() override
	{
		this->_ctx->_add_ready_task(_parent_vt.lock());
	}
};

void loop_comp_port(std::optional<std::chrono::steady_clock::time_point> until)
{
	HANDLE comp_port = comp_port::get_singleton().val;
	DWORD timeout = INFINITE;

	// HANDLE timer = CreateWaitableTimer(NULL, TRUE, NULL);
	// if (until.has_value())
	//	timeout = (until.value() - std::chrono::steady_clock::now()).;

	while (42)
	{
		DWORD bytes_transfered = 0;
		ULONG_PTR completion_key = 0;
		overlapped_t *ovlp = nullptr;
		BOOL success = GetQueuedCompletionStatus(comp_port, &bytes_transfered, &completion_key, reinterpret_cast<OVERLAPPED **>(&ovlp), timeout);
		// PostQueuedCompletionStatus

		timeout = 0;

		if (success == FALSE)
		{
			if (ovlp)
			{
				auto s = evl::__internal::socket_t::from(ovlp);
				closesocket(s->sock);
				// s->parent->remove_client(s);

				continue;
			}

			break; // timeout
		}

		if (ovlp->type == overlapped_type::o_accept)
		{
			auto s = evl::__internal::socket_t::from(ovlp);

			auto vt = std::static_pointer_cast<evl::__internal::vaccept_task>(s->_vt_init.lock());
			s->_vt_init = {};
			s->addr.s4 = *(sockaddr_in *)(vt->_accept_buffer.data() + (sizeof(sockaddr_t::s4) + 16));

			vt->_ctx->_add_ready_task(vt);

			continue;
		}

		if (ovlp->type == overlapped_type::o_connect)
		{
			auto s = evl::__internal::socket_t::from(ovlp);
			// TODO
			continue;
		}

		if (bytes_transfered == 0)
		{
			if (ovlp)
			{
				auto s = evl::__internal::socket_t::from(ovlp);
				closesocket(s->sock);
				// s->parent->remove_client(s);
			}

			continue;
		}

		if (ovlp->type == overlapped_type::o_recv)
		{
			auto s = evl::__internal::socket_t::from(ovlp);

			auto vt = s->_vt_recv.lock();
			s->_vt_recv = {};
			vt->_value.resize(bytes_transfered);
			vt->_ctx->_add_ready_task(vt);

			continue;
		}

		if (ovlp->type == overlapped_type::o_send)
		{
			auto s = evl::__internal::socket_t::from(ovlp);

			auto vt = s->_vt_send.lock();
			s->_vt_recv = {};
			vt->_value.erase(vt->_value.begin(), vt->_value.begin() + bytes_transfered);
			vt->_ctx->_add_ready_task(vt);

			continue;
		}
	}
}

/*
// test
void mylisten()
{
	wsa_initer::init();
	HANDLE comp_port = comp_port::get_singleton().val;

	auto listener = evl::__internal::socket_t::create_listen(comp_port);
	listener->prepare_accept();
	loop_comp_port(comp_port);
}
*/


void evl::__internal::context::_loop(std::shared_ptr<vtask_base> main_vt, std::coroutine_handle<> main_handle)
{
	wsa_initer::init();

	this->_add_ready_task(main_vt);

	while (!main_handle.done())
	{
		while (!this->_ready_tasks.empty())
		{
			auto vt = std::move(this->_ready_tasks.back());
			this->_ready_tasks.pop_back();
			vt->resume();
		}

		while (!this->_timers.empty())
		{
			auto tim = std::move(this->_timers.back());
			this->_timers.pop_back();
			std::this_thread::sleep_until(tim._time);
			this->_add_ready_task(tim._task);
		}

		loop_comp_port(std::nullopt);
	}
}

evl::__internal::task<void> evl::__internal::task_promise_type<void>::get_return_object()
{
	auto vt = std::make_shared<vtask<void>>(std::coroutine_handle<task_promise_type<void>>::from_promise(*this));
	this->_vt = std::weak_ptr(vt);
	return task<void>(std::move(vt));
}

evl::__internal::timer_task evl::__internal::async_sleep_until(std::chrono::steady_clock::time_point time)
{
	return evl::__internal::timer_task(time);
}

evl::__internal::timer_task evl::__internal::async_sleep(std::chrono::duration<int64_t, std::nano> dur)
{
	return evl::__internal::async_sleep_until(std::chrono::steady_clock::now() + dur);
}


evl::__internal::recv_task::return_type evl::__internal::recv_task::await_resume() { return std::move(this->_vt->_value); }

void evl::__internal::recv_task::_start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const
{
	this->_vt->_ctx = ctx;
	this->_vt->_parent_vt = std::move(parent_vt);

	this->_client->recv_async(&this->_vt->_wsa_buf);
	this->_client->_vt_recv = this->_vt;
}

evl::__internal::recv_task evl::__internal::tcp_client::recv(std::vector<char> &&buffer)
{
	recv_task t;

	t._vt = std::make_shared<vrecv_task>();
	t._client = this->_sock.get();
	t._vt->_value = std::move(buffer);
	t._vt->_wsa_buf.buf = t._vt->_value.data();
	t._vt->_wsa_buf.len = (ULONG)t._vt->_value.size();

	return t;
}

evl::__internal::send_task::return_type evl::__internal::send_task::await_resume() { return std::move(this->_vt->_value); }

void evl::__internal::send_task::_start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const
{
	this->_vt->_ctx = ctx;
	this->_vt->_parent_vt = std::move(parent_vt);

	this->_client->send_async(&this->_vt->_wsa_buf);
	this->_client->_vt_send = this->_vt;
}

evl::__internal::send_task evl::__internal::tcp_client::send(std::vector<char> &&buffer)
{
	send_task t;

	t._vt = std::make_shared<vsend_task>();
	t._client = this->_sock.get();
	t._vt->_value = std::move(buffer);
	t._vt->_wsa_buf.buf = t._vt->_value.data();
	t._vt->_wsa_buf.len = (ULONG)t._vt->_value.size();

	return t;
}

evl::__internal::tcp_client::tcp_client(tcp_client &&) = default;
evl::__internal::tcp_client::~tcp_client() = default;

evl::__internal::accept_task::return_type evl::__internal::accept_task::await_resume() { return std::move(this->_vt->_value); }

void evl::__internal::accept_task::_start(context *ctx, std::weak_ptr<vtask_base> parent_vt) const
{
	this->_vt->_ctx = ctx;
	this->_vt->_parent_vt = std::move(parent_vt);

	this->_vt->_accept_buffer.resize((sizeof(sockaddr_t::s4) + 16) * 2);

	this->_vt->_value._sock = this->_server->prepare_accept(this->_vt->_accept_buffer.data());
	this->_vt->_value._sock->_vt_init = this->_vt;
}

evl::__internal::accept_task::accept_task(accept_task &&) = default;
evl::__internal::accept_task::~accept_task() = default;

evl::__internal::tcp_server evl::__internal::tcp_server::create(const char *addr, uint16_t port)
{
	tcp_server s;

	s._sock = socket_t::create_listen(comp_port::get_singleton().val);

	return s;
}

evl::__internal::tcp_server::tcp_server(tcp_server &&) = default;
evl::__internal::tcp_server::~tcp_server() = default;

evl::__internal::accept_task evl::__internal::tcp_server::accept()
{
	accept_task t;

	t._vt = std::make_shared<vaccept_task>();
	t._server = this->_sock.get();

	return t;
}

