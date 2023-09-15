/*
 * Copyright (c) 2017-2023 zhllxt
 *
 * author   : zhllxt
 * email    : 37792738@qq.com
 * 
 * Distributed under the Boost Software License, Version 1.0. (See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */

#pragma once

#include <any>

#include <asio3/core/asio.hpp>
#include <asio3/core/fixed_capacity_vector.hpp>
#include <asio3/core/move_only_any.hpp>
#include <asio3/core/detail/netutil.hpp>

namespace asio::socks5
{
	enum class connect_result : std::uint8_t
	{
		succeeded = 0,
		general_socks_server_failure,
		connection_not_allowed_by_ruleset,
		network_unreachable,
		host_unreachable,
		connection_refused,
		ttl_expired,
		command_not_supported,
		address_type_not_supported,
	};

	enum class address_type : std::uint8_t
	{
		unknown = 0,
		ipv4    = 1,
		domain  = 3,
		ipv6    = 4,
	};

	enum class auth_method : std::uint8_t
	{
		anonymous         = 0x00, // X'00' NO AUTHENTICATION REQUIRED
		gssapi            = 0x01, // X'01' GSSAPI
		password          = 0x02, // X'02' USERNAME/PASSWORD
		//iana              = 0x03, // X'03' to X'7F' IANA ASSIGNED
		//reserved          = 0x80, // X'80' to X'FE' RESERVED FOR PRIVATE METHODS
		noacceptable      = 0xFF, // X'FF' NO ACCEPTABLE METHODS
	};

	enum class command : std::uint8_t
	{
		connect       = 0x01, // CONNECT X'01'
		bind          = 0x02, // BIND X'02'
		udp_associate = 0x03, // UDP ASSOCIATE X'03'
	};

	using auth_method_vector = std::experimental::fixed_capacity_vector<auth_method, 8>;

	struct option
	{
		std::string        proxy_address{};
		std::uint16_t      proxy_port{};

		std::string        dest_address{};
		std::uint16_t      dest_port{};

		std::string        username{};
		std::string        password{};

		auth_method_vector method{};

		command            cmd{};

		std::string        bound_address{};
		std::uint16_t      bound_port{};
	};

	struct handshake_info
	{
		asio::protocol     last_read_channel{};

		std::uint16_t      dest_port{};
		std::string        dest_address{};

		std::string        username{};
		std::string        password{};

		auth_method_vector method{};

		command            cmd{};

		address_type       addr_type{};

		asio::ip::tcp::endpoint client_endpoint{};

		// connect_bound_socket_type or udp_associate_bound_socket_type
		std::move_only_any bound_socket{};
	};

	struct auth_config
	{
		// you can declare a custom struct that derive from this, and redefine these two 
		// bound socket type to let the async_accept function create a custom bound socket.
		using connect_bound_socket_type = asio::ip::tcp::socket;
		using udp_associate_bound_socket_type = asio::ip::udp::socket;

		auth_method_vector supported_method{};

		std::function<bool(handshake_info&)> auth_function{};
	};
}

namespace socks5 = ::asio::socks5;
