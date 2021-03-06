//
// Created by permal on 7/1/17.
//

#include <smooth/core/network/IPv4.h>
#include <arpa/inet.h>
#include <cstdint>

namespace smooth
{
    namespace core
    {
        namespace network
        {

            IPv4::IPv4(const std::string& ip_number_as_string, uint16_t port) : InetAddress(ip_number_as_string, port)
            {
                memset(&sock_address, 0, sizeof(sock_address));
                sock_address.sin_family = AF_INET;
                sock_address.sin_port = htons(port);
                valid = inet_pton(AF_INET, ip_number_as_string .c_str(), &sock_address.sin_addr) == 1;
            }

            sockaddr* IPv4::get_socket_address()
            {
                return reinterpret_cast<sockaddr*>(&sock_address);
            }

            socklen_t IPv4::get_socket_address_length() const
            {
                return sizeof(sock_address);
            }
        };
    }
}