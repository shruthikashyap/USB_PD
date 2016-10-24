#include "i2c.h"
#include "timer.h"
#include "usb_pd_tcpc.h"
#include "usb_pd_tcpm.h"
#include "gpio.h"
#include "task.h"
#include "tcpci.h"
#include "tcpm.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "util.h"
#include "../include/usb_pd.h"

extern int tcpc_transmit(int port, enum tcpm_transmit_type type, uint16_t header,
		  const uint32_t *data);

void send_int(void)
{
	uint32_t payload[7];
	payload[0] = 268537956;
	while(0)
	{
		sleep(1);

		if(tcpc_transmit(0, TCPC_TX_SOP, PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK, PD_ROLE_UFP,
					  1, 1), (uint32_t*)payload) == EC_SUCCESS)
		{
			gpio_set_level(GPIO_TEST_GPIO1, 1);
		}
	
		sleep(1);

		gpio_set_level(GPIO_TEST_GPIO1, 0);
	}
		
	while(0)
	{
		if(pd_is_valid_input_voltage(5) > 0)
			gpio_set_level(GPIO_TEST_GPIO2, 1);
		else
			gpio_set_level(GPIO_TEST_GPIO1, 1);
		
		sleep(1);
	}
}

#if 0
uint8_t host_buffer[4] = {'B', 'V', 'Y', '\0'};

void send(int x)
{
	while(0)
	{
		sleep(1);

		gpio_set_level(GPIO_TEST_GPIO2, 1);
	
		sleep(1);

		gpio_set_level(GPIO_TEST_GPIO2, 0);
	}
}

void test_shr(void)
{
#if 0
		host_buffer[0] = 'a';
		host_buffer[1] = 'l';
		host_buffer[2] = 'o';
		host_buffer[3] = 'a';
		host_buffer[4] = 'l';
		host_buffer[5] = 'p';
		host_buffer[6] = 'l';
		host_buffer[7] = '\0';

	while(1)
	{
		tcpc_i2c_process(1, TCPC_ADDR_TO_PORT(0x9e), 4, &host_buffer[0], send);

		if(host_buffer[0] == 'B')
		{
			gpio_set_level(GPIO_TEST_GPIO2, 1);
			sleep(1);
			gpio_set_level(GPIO_TEST_GPIO2, 0);

		}

		sleep(1);
		//host_buffer[0] = 'F';
	}
#endif

	//int tcpci_tcpm_transmit(int port, enum tcpm_transmit_type type, uint16_t header, const uint32_t *data);
	const uint32_t data[] = {};
	tcpci_tcpm_transmit(1, TCPC_TX_SOP, 100, &data[0]);
}
#endif
