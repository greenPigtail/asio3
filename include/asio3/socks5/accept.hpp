/*
 * Copyright (c) 2017-2023 zhllxt
 *
 * author   : zhllxt
 * email    : 37792738@qq.com
 * 
 * Distributed under the Boost Software License, Version 1.0. (See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 *
 * socks5 protocol : https://www.ddhigh.com/2019/08/24/socks5-protocol.html
 * UDP Associate : https://blog.csdn.net/whatday/article/details/40183555
 */

#pragma once

#include <asio3/core/error.hpp>
#include <asio3/core/detail/netutil.hpp>

#include <asio3/socks5/core.hpp>
#include <asio3/socks5/error.hpp>

#include <asio3/tcp/connect.hpp>
#include <asio3/tcp/read.hpp>
#include <asio3/tcp/write.hpp>

#include <asio3/udp/core.hpp>

namespace asio::socks5::detail
{
	struct async_accept_op
	{
		template<typename AsyncStream, typename AuthConfig>
		auto operator()(auto state,
			std::reference_wrapper<AsyncStream> sock_ref,
			std::reference_wrapper<AuthConfig> auth_cfg_ref) -> void
		{
			using ::asio::detail::read;
			using ::asio::detail::write;
			using ::asio::detail::to_underlying;

			auto& sock = sock_ref.get();

			auth_config& auth_cfg = auth_cfg_ref.get();

			state.reset_cancellation_state(asio::enable_terminal_cancellation());

			asio::error_code ec{};

			asio::streambuf strbuf{};
			asio::mutable_buffer buffer{};
			std::size_t bytes{};
			char* p{};

			asio::ip::tcp::endpoint dst_endpoint{};

			handshake_info hdshak_info{};

			hdshak_info.client_endpoint = sock.remote_endpoint(ec);

			std::string  & dst_addr = hdshak_info.dest_address;
			std::uint16_t& dst_port = hdshak_info.dest_port;

			// The client connects to the server, and sends a version
			// identifier / method selection message :

			// +----+----------+----------+
			// |VER | NMETHODS | METHODS  |
			// +----+----------+----------+
			// | 1  |    1     | 1 to 255 |
			// +----+----------+----------+

			strbuf.consume(strbuf.size());

			auto [e1, n1] = co_await asio::async_read(
				sock, strbuf, asio::transfer_exactly(1 + 1), use_nothrow_deferred);
			if (e1)
				co_return{ e1, std::move(hdshak_info) };

			p = const_cast<char*>(static_cast<const char*>(strbuf.data().data()));

			if (std::uint8_t version = read<std::uint8_t>(p); version != std::uint8_t(0x05))
			{
				ec = socks5::make_error_code(socks5::error::unsupported_version);
				co_return{ ec, std::move(hdshak_info) };
			}

			std::uint8_t nmethods{};
			if (nmethods = read<std::uint8_t>(p); nmethods == std::uint8_t(0))
			{
				ec = socks5::make_error_code(socks5::error::no_acceptable_methods);
				co_return{ ec, std::move(hdshak_info) };
			}

			strbuf.consume(strbuf.size());

			auto [e2, n2] = co_await asio::async_read(
				sock, strbuf, asio::transfer_exactly(nmethods), use_nothrow_deferred);
			if (e2)
				co_return{ e2, std::move(hdshak_info) };

			p = const_cast<char*>(static_cast<const char*>(strbuf.data().data()));

			auth_method method = auth_method::noacceptable;

			for (std::uint8_t i = 0; method == auth_method::noacceptable && i < nmethods; ++i)
			{
				auth_method m1 = static_cast<auth_method>(read<std::uint8_t>(p));

				for(auth_method m2 : auth_cfg.supported_method)
				{
					if (m1 == m2)
					{
						method = m1;
						break;
					}
				}
			}

			hdshak_info.method.emplace_back(method);

            // +----+--------+
            // |VER | METHOD |
            // +----+--------+
            // | 1  |   1    |
            // +----+--------+

			strbuf.consume(strbuf.size());

			bytes  = 2;
			buffer = strbuf.prepare(bytes);
			p      = static_cast<char*>(buffer.data());

			write(p, std::uint8_t(0x05));                  // VER 
			write(p, std::uint8_t(to_underlying(method))); // METHOD 

			strbuf.commit(bytes);

			auto [e3, n3] = co_await asio::async_write(
				sock, strbuf, asio::transfer_exactly(bytes), use_nothrow_deferred);
			if (e3)
				co_return{ e3, std::move(hdshak_info) };

			if (method == auth_method::noacceptable)
			{
				ec = socks5::make_error_code(socks5::error::no_acceptable_methods);
				co_return{ ec, std::move(hdshak_info) };
			}
			if (method == auth_method::password)
			{
				std::string& username = hdshak_info.username;
				std::string& password = hdshak_info.password;
				std::uint8_t ulen{}, plen{};

				//         +----+------+----------+------+----------+
				//         |VER | ULEN |  UNAME   | PLEN |  PASSWD  |
				//         +----+------+----------+------+----------+
				//         | 1  |  1   | 1 to 255 |  1   | 1 to 255 |
				//         +----+------+----------+------+----------+
					
				strbuf.consume(strbuf.size());

				auto [e4, n4] = co_await asio::async_read(
					sock, strbuf, asio::transfer_exactly(1 + 1), use_nothrow_deferred);
				if (e4)
					co_return{ e4, std::move(hdshak_info) };

				p = const_cast<char*>(static_cast<const char*>(strbuf.data().data()));

				// The VER field contains the current version of the subnegotiation, which is X'01'.
				if (std::uint8_t version = read<std::uint8_t>(p); version != std::uint8_t(0x01))
				{
					ec = socks5::make_error_code(socks5::error::unsupported_authentication_version);
					co_return{ ec, std::move(hdshak_info) };
				}

				if (ulen = read<std::uint8_t>(p); ulen == std::uint8_t(0))
				{
					ec = socks5::make_error_code(socks5::error::authentication_failed);
					co_return{ ec, std::move(hdshak_info) };
				}

				// read username
				strbuf.consume(strbuf.size());

				auto [e5, n5] = co_await asio::async_read(
					sock, strbuf, asio::transfer_exactly(ulen), use_nothrow_deferred);
				if (e5)
					co_return{ e5, std::move(hdshak_info) };

				p = const_cast<char*>(static_cast<const char*>(strbuf.data().data()));

				username.assign(p, ulen);

				// read password len
				strbuf.consume(strbuf.size());

				auto [e6, n6] = co_await asio::async_read(
					sock, strbuf, asio::transfer_exactly(1), use_nothrow_deferred);
				if (e6)
					co_return{ e6, std::move(hdshak_info) };

				p = const_cast<char*>(static_cast<const char*>(strbuf.data().data()));

				if (plen = read<std::uint8_t>(p); plen == std::uint8_t(0))
				{
					ec = socks5::make_error_code(socks5::error::authentication_failed);
					co_return{ ec, std::move(hdshak_info) };
				}

				// read password
				strbuf.consume(strbuf.size());

				auto [e7, n7] = co_await asio::async_read(
					sock, strbuf, asio::transfer_exactly(plen), use_nothrow_deferred);
				if (e7)
					co_return{ e7, std::move(hdshak_info) };

				p = const_cast<char*>(static_cast<const char*>(strbuf.data().data()));

				password.assign(p, plen);

				// compare username and password
				if (!auth_cfg.auth_function(hdshak_info))
				{
					strbuf.consume(strbuf.size());

					bytes  = 2;
					buffer = strbuf.prepare(bytes);
					p      = static_cast<char*>(buffer.data());

					write(p, std::uint8_t(0x01));                                                // VER 
					write(p, std::uint8_t(to_underlying(socks5::error::authentication_failed))); // STATUS  

					strbuf.commit(bytes);

					auto [e8, n8] = co_await asio::async_write(
						sock, strbuf, asio::transfer_exactly(bytes), use_nothrow_deferred);

					ec = socks5::make_error_code(socks5::error::authentication_failed);
					co_return{ ec, std::move(hdshak_info) };
				}
				else
				{
					strbuf.consume(strbuf.size());

					bytes  = 2;
					buffer = strbuf.prepare(bytes);
					p      = static_cast<char*>(buffer.data());

					write(p, std::uint8_t(0x01)); // VER 
					write(p, std::uint8_t(0x00)); // STATUS  

					strbuf.commit(bytes);

					auto [e9, n9] = co_await asio::async_write(
						sock, strbuf, asio::transfer_exactly(bytes), use_nothrow_deferred);
					if (e9)
						co_return{ e9, std::move(hdshak_info) };
				}
			}

			//  +----+-----+-------+------+----------+----------+
			//  |VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
			//  +----+-----+-------+------+----------+----------+
			//  | 1  |  1  | X'00' |  1   | Variable |    2     |
			//  +----+-----+-------+------+----------+----------+

			strbuf.consume(strbuf.size());

			// 1. read the first 5 bytes : VER REP RSV ATYP [LEN]
			auto [ea, na] = co_await asio::async_read(
				sock, strbuf, asio::transfer_exactly(5), use_nothrow_deferred);
			if (ea)
				co_return{ ea, std::move(hdshak_info) };

			p = const_cast<char*>(static_cast<const char*>(strbuf.data().data()));

			// VER
			if (std::uint8_t ver = read<std::uint8_t>(p); ver != std::uint8_t(0x05))
			{
				ec = socks5::make_error_code(socks5::error::unsupported_version);
				co_return{ ec, std::move(hdshak_info) };
			}

			// CMD
			socks5::command cmd = static_cast<socks5::command>(read<std::uint8_t>(p));

			hdshak_info.cmd = cmd;

			// skip RSV.
			read<std::uint8_t>(p);

			socks5::address_type atyp = static_cast<socks5::address_type>(read<std::uint8_t>(p)); // ATYP
			std::uint8_t         alen =                                   read<std::uint8_t>(p);  // [LEN]

			hdshak_info.addr_type = atyp;

			// ATYP
			switch (atyp)
			{
			case socks5::address_type::ipv4  : bytes = 4    + 2 - 1; break; // IP V4 address: X'01'
			case socks5::address_type::domain: bytes = alen + 2 - 0; break; // DOMAINNAME: X'03'
			case socks5::address_type::ipv6  : bytes = 16   + 2 - 1; break; // IP V6 address: X'04'
			default:
			{
				ec = socks5::make_error_code(socks5::error::address_type_not_supported);
				co_return{ ec, std::move(hdshak_info) };
			}
			}

			strbuf.consume(strbuf.size());

			auto [eb, nb] = co_await asio::async_read(
				sock, strbuf, asio::transfer_exactly(bytes), use_nothrow_deferred);
			if (eb)
				co_return{ eb, std::move(hdshak_info) };

			p = const_cast<char*>(static_cast<const char*>(strbuf.data().data()));

			dst_addr = "";
			dst_port = 0;

			switch (atyp)
			{
			case socks5::address_type::ipv4: // IP V4 address: X'01'
			{
				asio::ip::address_v4::bytes_type addr{};
				addr[0] = alen;
				addr[1] = read<std::uint8_t>(p);
				addr[2] = read<std::uint8_t>(p);
				addr[3] = read<std::uint8_t>(p);
				dst_endpoint.address(asio::ip::address_v4(addr));
				dst_addr = dst_endpoint.address().to_string(ec);
				dst_port = read<std::uint16_t>(p);
				dst_endpoint.port(dst_port);
			}
			break;
			case socks5::address_type::domain: // DOMAINNAME: X'03'
			{
				std::string addr;
				addr.resize(alen);
				std::copy(p, p + alen, addr.data());
				p += alen;
				dst_addr = std::move(addr);
				dst_port = read<std::uint16_t>(p);
				dst_endpoint.port(dst_port);
			}
			break;
			case socks5::address_type::ipv6: // IP V6 address: X'04'
			{
				asio::ip::address_v6::bytes_type addr{};
				addr[0] = alen;
				for (int i = 1; i < 16; i++)
				{
					addr[i] = read<std::uint8_t>(p);
				}
				dst_endpoint.address(asio::ip::address_v6(addr));
				dst_addr = dst_endpoint.address().to_string(ec);
				dst_port = read<std::uint16_t>(p);
				dst_endpoint.port(dst_port);
			}
			break;
			}

			asio::ip::address bnd_addr = sock.local_endpoint(ec).address();
			std::uint16_t     bnd_port = sock.local_endpoint(ec).port();

			//         o  REP    Reply field:
			//             o  X'00' succeeded
			//             o  X'01' general SOCKS server failure
			//             o  X'02' connection not allowed by ruleset
			//             o  X'03' Network unreachable
			//             o  X'04' Host unreachable
			//             o  X'05' Connection refused
			//             o  X'06' TTL expired
			//             o  X'07' Command not supported
			//             o  X'08' Address type not supported
			//             o  X'09' to X'FF' unassigned
			
			std::uint8_t urep = std::uint8_t(0x00);

			if (dst_addr.empty() || dst_port == 0)
			{
				urep = std::uint8_t(socks5::connect_result::host_unreachable);
				ec = socks5::make_error_code(socks5::error::host_unreachable);
			}
			else if (cmd == socks5::command::connect)
			{
				using connect_socket_t = typename std::remove_cvref_t<AuthConfig>::connect_bound_socket_type;

				std::string str_port = std::to_string(dst_port);

				asio::ip::tcp::resolver resolver(sock.get_executor());
				auto [er, eps] = co_await resolver.async_resolve(dst_addr, str_port, use_nothrow_deferred);
				if (er || eps.empty())
				{
					urep = std::uint8_t(socks5::connect_result::host_unreachable);
					ec = er ? er : asio::error::host_not_found;
				}
				else
				{
					connect_socket_t bnd_socket(sock.get_executor());
					auto [ed, ep] = co_await asio::async_connect(bnd_socket, eps, use_nothrow_deferred);

					if (!ed)
					{
						hdshak_info.bound_socket = std::move(bnd_socket);
					}

					if (!ed)
						urep = std::uint8_t(0x00);
					else if (ed == asio::error::network_unreachable)
						urep = std::uint8_t(socks5::connect_result::network_unreachable);
					else if (ed == asio::error::host_unreachable || ed == asio::error::host_not_found)
						urep = std::uint8_t(socks5::connect_result::host_unreachable);
					else if (ed == asio::error::connection_refused)
						urep = std::uint8_t(socks5::connect_result::connection_refused);
					else
						urep = std::uint8_t(socks5::connect_result::general_socks_server_failure);

					ec = ed;
				}
			}
			else if (cmd == socks5::command::udp_associate)
			{
				asio::ip::udp bnd_protocol = asio::ip::udp::v4();

				// if the dest protocol is ipv4, bind local ipv4 protocol
				if /**/ (atyp == socks5::address_type::ipv4)
				{
					bnd_protocol = asio::ip::udp::v4();
				}
				// if the dest protocol is ipv6, bind local ipv6 protocol
				else if (atyp == socks5::address_type::ipv6)
				{
					bnd_protocol = asio::ip::udp::v6();
				}
				// if the dest id domain, bind local protocol as the same with the domain
				else if (atyp == socks5::address_type::domain)
				{
					std::string str_port = std::to_string(dst_port);

					asio::ip::udp::resolver resolver(sock.get_executor());
					auto [er, eps] = co_await resolver.async_resolve(dst_addr, str_port, use_nothrow_deferred);
					if (!er && !eps.empty())
					{
						if ((*eps).endpoint().address().is_v6())
							bnd_protocol = asio::ip::udp::v6();
						else
							bnd_protocol = asio::ip::udp::v4();
					}
					// if resolve domain failed, bind local protocol as the same with the tcp connection
					else
					{
						if (sock.local_endpoint(ec).address().is_v6())
							bnd_protocol = asio::ip::udp::v6();
						else
							bnd_protocol = asio::ip::udp::v4();
					}
				}

				using udpass_socket_t = typename std::remove_cvref_t<AuthConfig>::udp_associate_bound_socket_type;

				try
				{
					// port equal to 0 is means use a random port.
					udpass_socket_t bnd_socket(sock.get_executor(), asio::ip::udp::endpoint(bnd_protocol, 0));
					bnd_port = bnd_socket.local_endpoint().port();
					hdshak_info.bound_socket = std::move(bnd_socket);
				}
				catch (const asio::system_error& e)
				{
					ec = e.code();
				}

				if (!ec)
					urep = std::uint8_t(0x00);
				else
					urep = std::uint8_t(socks5::connect_result::general_socks_server_failure);
			}
			else/* if (cmd == socks5::command::bind)*/
			{
				urep = std::uint8_t(socks5::connect_result::command_not_supported);
				ec = socks5::make_error_code(socks5::error::command_not_supported);
			}

			strbuf.consume(strbuf.size());

			// the address field contains a fully-qualified domain name.  The first
			// octet of the address field contains the number of octets of name that
			// follow, there is no terminating NUL octet.
			buffer = strbuf.prepare(1 + 1 + 1 + 1 + (std::max)(16, int(dst_addr.size() + 1)) + 2);
			p      = static_cast<char*>(buffer.data());

			write(p, std::uint8_t(0x05)); // VER 5.
			write(p, std::uint8_t(urep)); // REP 
			write(p, std::uint8_t(0x00)); // RSV.
			write(p, std::uint8_t(atyp)); // ATYP 

			if (bnd_addr.is_v4())
			{
				// real length
				bytes = 1 + 1 + 1 + 1 + 4 + 2;

				write(p, bnd_addr.to_v4().to_uint());
			}
			else
			{
				// real length
				bytes = 1 + 1 + 1 + 1 + 16 + 2;

				auto addr_bytes = bnd_addr.to_v6().to_bytes();
				std::copy(addr_bytes.begin(), addr_bytes.end(), p);
				p += 16;
			}

			// port
			write(p, bnd_port);

			strbuf.commit(bytes);

			auto [ef, nf] = co_await asio::async_write(
				sock, strbuf, asio::transfer_exactly(bytes), use_nothrow_deferred);
			co_return{ ef ? ef : ec, std::move(hdshak_info) };
		}
	};
}

namespace asio::socks5
{
	/**
	 * @brief Perform the socks5 handshake asynchronously in the server role.
	 * @param socket - The read/write stream object reference.
	 * @param auth_cfg - The socks5 auth option reference.
	 * @param token - The completion handler to invoke when the operation completes.
	 *	  The equivalent function signature of the handler must be:
	 *    @code
	 *    void handler(const asio::error_code& ec, socks5::handshake_info info);
	 */
	template<
		typename AsyncStream, typename AuthConfig,
		ASIO_COMPLETION_TOKEN_FOR(void(asio::error_code, socks5::handshake_info)) AcceptToken
		ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(typename AsyncStream::executor_type)>
	requires std::derived_from<std::remove_cvref_t<AuthConfig>, socks5::auth_config>
	ASIO_INITFN_AUTO_RESULT_TYPE_PREFIX(AcceptToken, void(asio::error_code, socks5::handshake_info))
	async_accept(
		AsyncStream& sock, AuthConfig& auth_cfg,
		AcceptToken&& token ASIO_DEFAULT_COMPLETION_TOKEN(typename AsyncStream::executor_type))
	{
		return asio::async_initiate<AcceptToken, void(asio::error_code, socks5::handshake_info)>(
			asio::experimental::co_composed<void(asio::error_code, socks5::handshake_info)>(
				detail::async_accept_op{}, sock),
			token, std::ref(sock), std::ref(auth_cfg));
	}
}
