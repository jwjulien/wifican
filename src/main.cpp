/* =====================================================================================================================
 *      File:  /src/main.cpp
 *   Project:  WiFi CAN
 *    Author:  Jared Julien <jaredjulien@exsystems.net>
 * Copyright:  (c) 2024 Jared Julien, eX Systems
 * ---------------------------------------------------------------------------------------------------------------------
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ---------------------------------------------------------------------------------------------------------------------
 */
// =====================================================================================================================
// Includes
// ---------------------------------------------------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

extern "C" {
	#include "can2040.h"
}




//======================================================================================================================
// Constants
//----------------------------------------------------------------------------------------------------------------------
// Pin definitions to match the hardware.
#define PIN_DOUT 14
#define PIN_DIN 15
#define PIN_NEOPIXEL 11
#define PIN_CAN_TX 16
#define PIN_CAN_RX 17
#define PIN_BOOT 18

// Common color definitions for use with the status NeoPixel LED.  Others can be added as needed.  These are just hex
// color codes pulled from the internet.
#define COLOR_BLACK 0x00000000
#define COLOR_WHITE 0x00FFFFFF
#define COLOR_RED 0x00FF0000
#define COLOR_ORANGE 0X00FF8C00
#define COLOR_YELLOW 0x00FFFF00
#define COLOR_GREEN 0x0000FF00
#define COLOR_BLUE 0x000000FF
#define COLOR_PURPLE 0x00800080
#define COLOR_MAGENTA 0x00FF00FF
#define COLOR_CYAN 0x0000FFFF

#define CLIENT_MODE 1
#define AP_MODE 2



//======================================================================================================================
// Configuration
//----------------------------------------------------------------------------------------------------------------------
// Adjust the parameters in this section to control device behavior.

// Version number settings are broadcast in the heartbeat message.  The purpose is to help indicate what version of this
// software is running on devices in the field.  Change these to suit your application.
#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 0

// CAN bus settings.
#define CAN_BITRATE 500000  // Currently the baud rate is hard coded here.
#define QUEUE_DEPTH 20      // Sets the buffer size for number of received CAN messages for retransmission to WiFi.

// TCP port to which clients shall connect.
#define TCP_PORT 10001

/* WiFi connection settings.
 * If CLIENT_MODE is:
 * - AP_MODE: WIFI_SSID and WIFI_PASS are used to set the network name and password for the provided access point.
 *       Ensure that the SSID is unique to this device.  Consider appending a random string to the name.
 * - CLIENT_MODE: WIFI_SSID and WIFI_PASS should be credentials to connect to an existing WiFi network.
 */
#define WIFI_MODE AP_MODE
#define WIFI_SSID "WiFiCAN-53b2ce"      // When in AP mode ensure this name is unique to this device.
#define WIFI_PASS "password123"

// Maximum number of clients that can be connected to the WiFi socket at one time.
// NOTE: RPi Pico W hardware supports only 4 clients MAX.  No point in making this number larger.
#define MAX_CLIENTS 4




//======================================================================================================================
// Module Variables
//----------------------------------------------------------------------------------------------------------------------
static struct can2040 cbus;
static struct can2040_msg version_msg;
static WiFiServer server(TCP_PORT);
static WiFiClient *clients[MAX_CLIENTS] = { NULL };
Adafruit_NeoPixel pixel = Adafruit_NeoPixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static struct can2040_msg queue[QUEUE_DEPTH];
static uint8_t queue_head = 0;
static uint8_t queue_tail = 0;
static uint8_t queue_count = 0;
static volatile bool error = false;




//======================================================================================================================
// Functions
//----------------------------------------------------------------------------------------------------------------------
/* Set the status LED to the provided color.
 *
 * The LED is a WS2812B NeoPixel which requires a special serial protocol for communication.  To handle that, the
 * Adafruit NeoPixel library was used, but that comes with some bloat so this method handles that heavy lifting to make
 * things as simple as possible.
 *
 * Arguments:
 *   - color: uint32 containing the 0xXXRRGGBB RGB color value to set on the status LED.
 */
static void set_led(uint32_t color)
{
	pixel.setPixelColor(0, color);
	pixel.show();
}


//----------------------------------------------------------------------------------------------------------------------
/* Helper function to pad String instances with leading zeros up to the provided `length`.
 *
 * Arguments:
 *   - string: Original string object to be modified.
 *   - length: Minimum integer number of characters in `string`.  If `string` length is less than this number zeros will
 *       will be prepended.
 *
 * Returns:
 *   The potentially modified version of `string`.
 */
static String pad_left_zeros(String string, uint8_t length)
{
	while (string.length() < length)
	{
		string = "0" + string;
	}

	return string;
}


//----------------------------------------------------------------------------------------------------------------------
/* Queue a can2040 message for later transmission.
 *
 * This ring buffer message queue is used to shelve messages received by the physical CAN bus for later retransmission
 * to connected WiFi clients from the loop function.  Messages are received via callback from can2040 which comes from
 * an ISR and needs to be serviced quickly.
 *
 * Arguments:
 *   - msg: A pointer of the received CAN message object to be *copied* into the queue.
 *
 * Returns:
 *   - 0: No error - message was queued successfully.
 *   - 1: Queue full error - no action taken and `msg` will be ignored.
 */
static uint8_t enqueue(can2040_msg * msg)
{
	if (queue_count >= QUEUE_DEPTH)
	{
		return 1;
	}

	queue[queue_head].id = msg->id;
	queue[queue_head].dlc = msg->dlc;
	queue[queue_head].data32[0] = msg->data32[0];
	queue[queue_head].data32[1] = msg->data32[1];
	queue_head = (queue_head + 1) % QUEUE_DEPTH;
	queue_count++;
	return 0;
}


//----------------------------------------------------------------------------------------------------------------------
/* Pop a message from the queue and place into the provided pointer.
 *
 * This ring buffer message queue is used to shelve messages received by the physical CAN bus for later retransmission
 * to connected WiFi clients from the loop function.  Messages are received via callback from can2040 which comes from
 * an ISR and needs to be serviced quickly.
 *
 * Arguments:
 *   - msg: Pointer to a can2040 message object into which the next available message will be copied.
 *
 * Returns:
 *   - 0: No error - a message is available in `msg` for further processing.
 *   - 1: Queue is empty.  `msg` was not modified.
 */
static uint8_t dequeue(can2040_msg * msg)
{
	if (queue_count == 0)
	{
		return 1;
	}

	msg->id = queue[queue_tail].id;
	msg->dlc = queue[queue_tail].dlc;
	msg->data32[0] = queue[queue_tail].data32[0];
	msg->data32[1] = queue[queue_tail].data32[1];
	queue_tail = (queue_tail + 1) % QUEUE_DEPTH;
	queue_count--;
	return 0;
}


//----------------------------------------------------------------------------------------------------------------------
/* Transmit the provided CAN message to ALL of the connected clients on the WiFi/socket connection.
 *
 * Arguments:
 *   - msg: Pointer to a CAN message to be formatted and transmitted to all of the currently connected clients.
 */
static void broadcast_received_message(can2040_msg * msg)
{
	String payload = ":";
	bool extended = msg->id & CAN2040_ID_EFF ? true : false;
	payload += extended ? 'X' : 'S';
	payload += pad_left_zeros(String(msg->id & 0x1FFFFFFF, 16), 3);
	payload += 'N';
	for (uint8_t idx = 0; idx < msg->dlc; idx++)
	{
		payload += pad_left_zeros(String(msg->data[idx], 16), 2);
	}
	payload += ';';

	for (uint8_t idx = 0; idx < MAX_CLIENTS; idx++)
	{
		if (clients[idx] != NULL)
		{
			clients[idx]->print(payload);
		}
	}
}


//----------------------------------------------------------------------------------------------------------------------
static void can2040_cb(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg)
{
	if (notify == CAN2040_NOTIFY_RX)
	{
		// Queue received messages to minimize time spent in this ISR context.
		enqueue(msg);
	}
	else if (notify == CAN2040_NOTIFY_ERROR)
	{
		error = true;
	}
}


//----------------------------------------------------------------------------------------------------------------------
static void PIOx_IRQHandler(void)
{
	can2040_pio_irq_handler(&cbus);
}


//----------------------------------------------------------------------------------------------------------------------
static void canbus_setup(void)
{
	can2040_setup(&cbus, 0);
	can2040_callback_config(&cbus, can2040_cb);

	// Enable IRQs.
	irq_set_exclusive_handler(PIO0_IRQ_0_IRQn, PIOx_IRQHandler);
	NVIC_SetPriority(PIO0_IRQ_0_IRQn, 1);
	NVIC_EnableIRQ(PIO0_IRQ_0_IRQn);

	// Start CAN bus.
	can2040_start(&cbus, F_CPU, CAN_BITRATE, PIN_CAN_RX, PIN_CAN_TX);
}


//----------------------------------------------------------------------------------------------------------------------
/* Transmit the provided CAN data from a WiFi/socket client on the physical bus.
 *
 * Arguments:
 *   - data: Raw string received from the WiFI client to be parsed into a can2040 CAN message object and transmit.
 */
static void transmit_can_message(String data)
{
	can2040_msg msg;

	// Data is not case sensitive, so convert to uppercase.
	data.toUpperCase();

	// Parse the message ID and determine if this was an extended message.
	uint16_t offset = data.indexOf('N');
	msg.id = strtol(data.substring(2, offset).c_str(), 0, 16);
	if (data[1] == 'X')
	{
		// Inform the CAN driver this is an extended message.
		msg.id |= CAN2040_ID_EFF;
	}
	offset += 1;

	// Parse the message data.
	uint8_t idx = 0;
	while ((offset < data.length()) && (idx < 8))
	{
		msg.data[idx] = strtol(data.substring(offset, offset + 2).c_str(), 0, 16);
		idx += 1;
		offset += 2;
	}

	// Set the DLC to match the number of data bytes provided.
	msg.dlc = idx;

	// Transmit this message on the physical CAN bus.
	can2040_transmit(&cbus, &msg);
}




//======================================================================================================================
// Setup Function
//----------------------------------------------------------------------------------------------------------------------
void setup(void)
{
	// Start with the status LED init so we can immediately show our status.
	pixel.begin();
	pixel.setBrightness(16);
	set_led(COLOR_YELLOW);

	// Wait a bit for a serial connection to be established to facilitate debugging this startup sequence.
	Serial.begin(115200);
	uint32_t start = millis();
	while (!Serial && (millis() - start) < 8000);
	set_led(COLOR_CYAN);
	Serial.println(F("Raspberry Pi Pico W powered WiFi / CAN Bridge"));

	// Setup the CAN bus.
	Serial.print(F("Configuring CAN..."));
	canbus_setup();
	Serial.println(F("bus up"));

	// Either setup of connect to WiFi based upon the settings above.
#if (WIFI_MODE == CLIENT_MODE)
	Serial.print(F("Connecting to "));
	Serial.print(WIFI_SSID);
	Serial.print(F("..."));
	uint8_t status = WiFi.begin(WIFI_SSID, WIFI_PASS);
#elif (WIFI_MODE == AP_MODE)
	Serial.print(F("Starting access point "));
	Serial.print(WIFI_SSID);
	Serial.print(F("..."));
	uint8_t status = WiFi.beginAP(WIFI_SSID, WIFI_PASS);
#else
#  error Invalid WIFI_MODE selection.  Please check your configuration settings.
#endif
	if (status != WL_CONNECTED)
	{
		Serial.println(F("failed"));
		set_led(COLOR_RED);
		while (1);
	}
	Serial.println(F("listening"));

	// Print out IP address.
	Serial.print(F("Assigned IP address: "));
	Serial.println(WiFi.localIP());

	// Set up the server to handle socket connections.
	Serial.print(F("Starting server on port "));
	Serial.print(TCP_PORT);
	Serial.print(F("..."));
	server.begin();
	Serial.println(F("started"));

	// Setup a periodic message to transmit version info about this interface and provide a heartbeat.
	// Message ID was arbitrarily chosen and intended to be very low priority and likely unused.  Change if needed.
	version_msg.id = 0x1FFFFF22 | CAN2040_ID_EFF;
	version_msg.dlc = 3;
	version_msg.data[0] = VERSION_MAJOR;
	version_msg.data[1] = VERSION_MINOR;
	version_msg.data[2] = VERSION_PATCH;

	// Indicate successful setup by switching status LED to green.
	set_led(COLOR_GREEN);
}




//======================================================================================================================
// Loop Function
//----------------------------------------------------------------------------------------------------------------------
void loop(void)
{
	static uint32_t previous = 0;
	static String received[MAX_CLIENTS];

	// Heartbeat indicators.
	if ((millis() - previous) > 500)
	{
		previous += 500;

		// Send the heartbeat version message to all of the interfaces.
		can2040_transmit(&cbus, &version_msg);
		broadcast_received_message(&version_msg);

		// Blink the status LED.
		uint32_t color = error ? COLOR_RED : COLOR_GREEN;
		set_led((millis() / 1000) & 1 ? color : COLOR_BLACK);

		// Print to the debug serial monitor.
		Serial.print('.');
	}

	// Check if a new client is connected.
	WiFiClient new_client = server.accept();
	if (new_client)
	{
		// Find the first unused slot.
		for (uint8_t idx = 0; idx < MAX_CLIENTS; idx++)
		{
			// Locate an open client slot that we can use.
			if (clients[idx] == NULL)
			{
				Serial.print(F("New client: "));
				Serial.println(idx);
				clients[idx] = new WiFiClient(new_client);
				break;
			}
		}
	}

	// Check if any connected client has data.
	for (uint8_t idx = 0; idx < MAX_CLIENTS; idx++)
	{
		if (clients[idx] == NULL)
		{
			continue;
		}

		// If the client is in use and has some data...
		if (clients[idx]->available())
		{
			// Read the data.
			char c = clients[idx]->read();
			if (c == ':')
			{
				received[idx] = ':';
			}
			else if (c == ';')
			{
				received[idx] += ';';

				// Send this message to the physical CAN bus.
				transmit_can_message(received[idx]);

				// Retransmit this message to other TCP clients too.
				for (uint8_t other = 0; other < MAX_CLIENTS; other++)
				{
					// Don't send back to the origin client.
					if ((idx != other) && (clients[other] != NULL))
					{
						clients[other]->print(received[idx]);
					}
				}
			}
			else
			{
				received[idx] += c;
			}
		}

		// Remove clients that have disconnected poorly.
		if (!clients[idx]->connected())
		{
			Serial.print(F("Client "));
			Serial.print(idx);
			Serial.println(F(" has disconnected"));

			// Clean up the client.
			clients[idx]->stop();
			delete clients[idx];
			clients[idx] = NULL;
		}
	}

	// Transmit queued CAN messages to connected clients.
	can2040_msg rx_msg;
	if (dequeue(&rx_msg) == 0)
	{
		broadcast_received_message(&rx_msg);
	}
}




/* End of File */
