//
// Created by permal on 8/29/17.
//

#include <smooth/core/io/Output.h>

namespace smooth
{
    namespace core
    {
        namespace io
        {
            Output::Output(gpio_num_t io, bool active_high, bool pull_up, bool pull_down, bool clear_on_creation)
                    : io(io),
                      active_high(active_high)
            {
                gpio_config_t config;

                config.pin_bit_mask = 1;
                config.pin_bit_mask <<= io;

                config.mode = active_high ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT_OUTPUT_OD;
                config.pull_down_en = pull_down ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
                config.pull_up_en = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
                config.intr_type = GPIO_INTR_DISABLE;

                ESP_ERROR_CHECK(gpio_config(&config));

                if (clear_on_creation)
                {
                    clr();
                }
            }

            void Output::set()
            {
                gpio_set_level(io, active_high ? 1 : 0);
            }

            void Output::set(bool active)
            {
                if(active)
                {
                    set();
                }
                else
                {
                    clr();
                }
            }

            void Output::clr()
            {
                gpio_set_level(io, active_high ? 0 : 1);
            }
        };
    }
}
