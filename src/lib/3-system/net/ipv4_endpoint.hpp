#ifndef ca_system_net_ipv4_endpoint_hpp
#define ca_system_net_ipv4_endpoint_hpp

#include "1-primitives/debug/trap.hpp"
#include "1-primitives/integers/u16.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u8.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- IPV4 ENDPOINT --------------------------------------------------

		namespace impl {

			[[noreturn, gnu::cold, gnu::noinline]] inline auto octet_out_of_range(void) noexcept -> void {
				ca::trap();
			}

		} // namespace impl


		/* @DOC An IPv4 address and a port, both in host order: what sockaddr_in holds, without
		   <netinet/in.h>. The default is 0.0.0.0 port 0 (any address, a port the system picks). */
		class ipv4_endpoint final {


			private:

				// -- private types ------------------------------------------

				using self = ca::sys::ipv4_endpoint;


				// -- private members ----------------------------------------

				ca::u32 _address;
				ca::u16 _port;


			public:

				// -- public lifecycle ---------------------------------------

				constexpr ipv4_endpoint(void) noexcept
				: _address{0U}, _port{0U} {
				}

				/* @DOC address as one integer, host order (127.0.0.1 is 0x7F000001). */
				constexpr ipv4_endpoint(const ca::u32 address, const ca::u16 port) noexcept
				: _address{address}, _port{port} {
				}

				/* @DOC a.b.c.d port. */
				constexpr ipv4_endpoint(const ca::u8 a, const ca::u8 b, const ca::u8 c, const ca::u8 d, const ca::u16 port) noexcept
				: _address{(static_cast<ca::u32>(a) << 24U) | (static_cast<ca::u32>(b) << 16U) | (static_cast<ca::u32>(c) << 8U) | static_cast<ca::u32>(d)}, _port{port} {
				}

				/* @DOC 127.0.0.1 port. */
				[[nodiscard]] static constexpr auto loopback(const ca::u16 port) noexcept -> self {
					return self{0x7F000001U, port};
				}

				/* @DOC 0.0.0.0 port: every local address, for bind. */
				[[nodiscard]] static constexpr auto any(const ca::u16 port) noexcept -> self {
					return self{0U, port};
				}


				// -- accessors ----------------------------------------------

				/* @DOC The address as one integer, host order. */
				[[nodiscard]] constexpr auto address(void) const noexcept -> ca::u32 {
					return _address;
				}

				/* @DOC Byte index of the dotted form, 0 to 3, a.b.c.d; traps past 3. */
				[[nodiscard]] constexpr auto octet(const ca::u32 index) const noexcept -> ca::u8 {

					if (index > 3U) [[unlikely]]
						ca::sys::impl::octet_out_of_range();
					return static_cast<ca::u8>(_address >> (8U * (3U - index)));
				}

				/* @DOC The port, host order. */
				[[nodiscard]] constexpr auto port(void) const noexcept -> ca::u16 {
					return _port;
				}


				// -- comparison ---------------------------------------------

				[[nodiscard]] friend constexpr auto operator==(const self& a, const self& b) noexcept -> bool {
					return a._address == b._address && a._port == b._port;
				}

		}; // class ipv4_endpoint

	} // namespace sys

} // namespace ca

#endif // ca_system_net_ipv4_endpoint_hpp
