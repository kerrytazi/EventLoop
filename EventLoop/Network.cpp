#include "Network.hpp"


evl::__internal::__network::recv_task::return_type evl::__internal::__network::tcp_client_buffered::_recv_line(std::span<char> done_part, std::vector<char> &line)
{
	auto it = std::ranges::find(done_part, '\n');

	if (it != done_part.end())
	{
		auto result_size = std::distance(this->_recv_buf.data(), &*it);
		auto result = std::span(this->_recv_buf.data(), result_size);

		line.insert(line.end(), result.begin(), result.end());

		auto left_part = std::span(std::next(it), done_part.end());
		this->_offset = left_part.size();
		std::ranges::copy(left_part, this->_recv_buf.begin());

		return result_size;
	}

	return -1;
}

evl::__internal::task<evl::__internal::__network::recv_task::return_type> evl::__internal::__network::tcp_client_buffered::recv_all(std::span<char> data)
{
	if (this->_recv_buf.size() < data.size())
		this->_recv_buf.resize(data.size());

	auto buf_part = std::span(this->_recv_buf);

	while (this->_offset < data.size())
		this->_offset += co_await this->recv(buf_part.subspan(this->_offset));

	auto done_part = std::span(this->_recv_buf.data(), data.size());
	std::ranges::copy(done_part, data.begin());
	this->_offset -= data.size();
	auto left_part = std::span(this->_recv_buf.data() + data.size(), this->_offset);
	std::ranges::copy(left_part, this->_recv_buf.data());
	co_return data.size();
}

evl::__internal::task<evl::__internal::__network::recv_task::return_type> evl::__internal::__network::tcp_client_buffered::recv_line(std::vector<char> &line)
{
	if (this->_offset > 0)
	{
		auto done_part = std::span(this->_recv_buf.data(), this->_offset);

		if (auto result_size = this->_recv_line(done_part, line); result_size != -1)
			co_return result_size;
	}

	while (42)
	{
		{
			auto recv_part = std::span(
				this->_recv_buf.data() + this->_offset,
				this->_recv_buf.size() - this->_offset
			);

			size_t done = co_await this->_client.recv(recv_part);

			auto done_part = recv_part.subspan(0, done);

			if (auto result_size = this->_recv_line(done_part, line); result_size != -1)
				co_return result_size;

			this->_offset += done;
		}

		if (this->_offset + 1024 > this->_recv_buf.size())
			this->_recv_buf.resize(this->_recv_buf.size() + 1024);
	}
}

evl::__internal::__network::tcp_client_buffered::tcp_client_buffered(evl::__internal::__network::tcp_client &&client) :
	_client(std::move(client))
{
	this->_recv_buf.resize(64 * 1024);
}


evl::__internal::__network::tcp_client_buffered evl::__internal::__network::tcp_client::make_buffered() && { return tcp_client_buffered(std::move(*this)); }

