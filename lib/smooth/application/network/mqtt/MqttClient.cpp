//
// Created by permal on 7/20/17.
//

#include <smooth/application/network/mqtt/MqttClient.h>
#include <smooth/application/network/mqtt/packet/Connect.h>
#include <smooth/application/network/mqtt/state/StartupState.h>
#include <smooth/application/network/mqtt/state/DisconnectState.h>
#include <smooth/application/network/mqtt/event/ConnectEvent.h>
#include <smooth/application/network/mqtt/event/DisconnectEvent.h>
#include <smooth/core/logging/log.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

using namespace smooth::core::ipc;
using namespace smooth::core::timer;
using namespace smooth::core::logging;
using namespace std::chrono;

namespace smooth
{
    namespace application
    {
        namespace network
        {
            namespace mqtt
            {
                MqttClient::MqttClient(const std::string& mqtt_client_id,
                                       std::chrono::seconds keep_alive,
                                       uint32_t stack_size,
                                       uint32_t priority, TaskEventQueue<MQTTData>& application_queue)
                        : Task(mqtt_client_id, stack_size, priority, std::chrono::milliseconds(50)),
                          application_queue(application_queue),
                          tx_empty("TX_empty", 5, *this, *this),
                          data_available("data_available", 5, *this, *this),
                          connection_status("connection_status", 5, *this, *this),
                          timer_events("timer_events", 5, *this, *this),
                          control_event("control_event", 5, *this, *this),
                          system_event("system_event", 5, *this, *this),
                          guard(),
                          client_id(mqtt_client_id),
                          keep_alive(keep_alive),
                          mqtt_socket(),
                          reconnect_timer(
                                  Timer::create("reconnect_timer",
                                                MQTT_FSM_RECONNECT_TIMER_ID,
                                                timer_events,
                                                false,
                                                std::chrono::seconds(5))),
                          keep_alive_timer(
                                  Timer::create("keep_alive_timer",
                                                MQTT_FSM_KEEP_ALIVE_TIMER_ID,
                                                timer_events,
                                                true,
                                                std::chrono::seconds(1))),
                          fsm(*this),
                          address()
                {
                }

                void MqttClient::tick()
                {
                    std::lock_guard<std::mutex> lock(guard);
                    fsm.tick();
                }

                void MqttClient::init()
                {
#ifdef ESP_PLATFORM
                    esp_log_level_set(mqtt_log_tag, static_cast<esp_log_level_t>(CONFIG_SMOOTH_MQTT_LOGGING_LEVEL));
#endif

                    fsm.set_state(new(fsm) state::StartupState(fsm));
                }

                void MqttClient::connect_to(std::shared_ptr<smooth::core::network::InetAddress> address,
                                            bool auto_reconnect)
                {
                    std::lock_guard<std::mutex> lock(guard);
                    // Must start the task before pushing an event, otherwise the task will not
                    // have run its exec() far enough to initialize the FreeRTOS pointer
                    start();

                    if(address)
                    {
                        {
                            std::lock_guard<std::mutex> l(address_guard);
                            this->address = address;
                        }
                        this->auto_reconnect = auto_reconnect;
                        control_event.push(event::ConnectEvent());
                    }
                }

                bool MqttClient::publish(const std::string& topic, const std::string& msg, mqtt::QoS qos, bool retain)
                {
                    return publish(topic,
                                   reinterpret_cast<const uint8_t*>(msg.c_str()),
                                   msg.size(),
                                   qos,
                                   retain);
                }

                bool MqttClient::publish(const std::string& topic, const uint8_t* data, int length, mqtt::QoS qos,
                                         bool retain)
                {
                    std::lock_guard<std::mutex> lock(guard);
                    return publication.publish(topic, data, length, qos, retain);
                }

                void MqttClient::subscribe(const std::string& topic, QoS qos)
                {
                    std::lock_guard<std::mutex> lock(guard);
                    subscription.subscribe(topic, qos);
                }

                void MqttClient::unsubscribe(const std::string& topic)
                {
                    std::lock_guard<std::mutex> lock(guard);
                    subscription.unsubscribe(topic);
                }

                void MqttClient::disconnect()
                {
                    control_event.push(event::DisconnectEvent());
                }

                void MqttClient::force_disconnect()
                {
                    fsm.set_state(new(fsm) state::DisconnectState(fsm));
                }

                bool MqttClient::send_packet(packet::MQTTPacket& packet)
                {
                    packet.dump("Outgoing");

                    bool res = false;
                    if (packet.validate_packet())
                    {
                        res = tx_buffer.put(packet);
                    }
                    return res;
                }

                const std::string& MqttClient::get_client_id() const
                {
                    return client_id;
                }

                const std::chrono::seconds MqttClient::get_keep_alive() const
                {
                    return keep_alive;
                }

                void MqttClient::start_reconnect()
                {
                    reconnect_timer->start();
                }

                void MqttClient::set_keep_alive_timer(std::chrono::seconds interval)
                {
                    if (interval.count() == 0)
                    {
                        keep_alive_timer->stop();
                    }
                    else
                    {
                        std::chrono::milliseconds ms = interval;
                        ms /= 2;
                        keep_alive_timer->start(ms);
                    }
                }

                void MqttClient::event(const core::network::TransmitBufferEmptyEvent& event)
                {
                    fsm.event(event);
                }

                void MqttClient::event(const core::network::ConnectionStatusEvent& event)
                {
                    Log::info(mqtt_log_tag, Format("MQTT {1} server",
                                                   Str(event.is_connected() ? "connected to" : "disconnected from")));
                    connected = event.is_connected();
                    fsm.event(event);
                }

                void MqttClient::event(const core::network::DataAvailableEvent<packet::MQTTPacket>& event)
                {
                    packet::MQTTPacket p;
                    if (event.get(p))
                    {
                        fsm.packet_received(p);
                    }
                }

                void MqttClient::event(const core::timer::TimerExpiredEvent& event)
                {
                    fsm.event(event);
                }

                void MqttClient::event(const event::BaseEvent& event)
                {
                    if (event.get_type() == event::BaseEvent::DISCONNECT)
                    {
                        keep_alive_timer->stop();
                        reconnect_timer->stop();
                        tx_buffer.clear();
                        rx_buffer.clear();

                        if(mqtt_socket)
                        {
                            mqtt_socket->stop();
                            mqtt_socket.reset();
                        }
                    }
                    else if (event.get_type() == event::BaseEvent::CONNECT)
                    {
                        if (mqtt_socket)
                        {
                            mqtt_socket->stop();
                        }

                        tx_buffer.clear();
                        rx_buffer.clear();

                        mqtt_socket = core::network::Socket<packet::MQTTPacket>::create(tx_buffer,
                                                                                        rx_buffer,
                                                                                        tx_empty,
                                                                                        data_available,
                                                                                        connection_status);
                        mqtt_socket->start(address);
                    }
                }

                void MqttClient::event(const smooth::core::network::NetworkStatus& event)
                {
                    if (event.get_event() == smooth::core::network::NetworkEvent::GOT_IP)
                    {
                        reconnect();
                    }
                }

                std::string MqttClient::get_payload(const MQTTData& data)
                {
                    std::stringstream ss;
                    for(auto b : data.second)
                    {
                        ss << static_cast<char>(b);
                    }

                    return ss.str();
                }
            }
        }
    }
}