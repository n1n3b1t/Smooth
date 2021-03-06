//
// Created by permal on 7/22/17.
//

#pragma once

#include <smooth/application/network/mqtt/packet/MQTTPacket.h>

namespace smooth
{
    namespace application
    {
        namespace network
        {
            namespace mqtt
            {
                namespace packet
                {
                    class Disconnect
                            : public MQTTPacket
                    {
                        public:
                            Disconnect();
                    };
                }
            }
        }
    }
}
