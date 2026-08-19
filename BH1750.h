/*

  This is a library for the BH1750 Digital Light Sensor.

  The BH1750 board uses I2C for communication. Two pins are required to
  interface to the device. Configuring the I2C bus is expected to be done
  in user code. reduce only to HIGH_RES_MODE (1 Lux precision)

  Datasheet:
  http://www.elechouse.com/elechouse/images/product/Digital%20light%20Sensor/bh1750fvi-e.pdf

*/

#ifndef BH1750_h
#define BH1750_h

#define USE_DEBUG_1750

#include <Arduino.h>
#include "Wire.h"

// Uncomment, to enable debug messages
// #define BH1750_DEBUG

// No active state
#define BH1750_POWER_DOWN 0x00

// Waiting for measurement command
#define BH1750_POWER_ON 0x01

// Reset data register value - not accepted in POWER_DOWN mode
#define BH1750_RESET 0x07

// Default MTreg value
#define BH1750_DEFAULT_MTREG 69
#define BH1750_MTREG_MIN 31
#define BH1750_MTREG_MAX 254
#define BH1750_CONTINUOUS_HIGH_RES_MODE		0x10		// Measurement at 1 lux resolution. Measurement time is approx 120ms.
#define BH1750_I2C_ADDRESS                  0x23

class BH1750
{
	public:

		BH1750();
		bool begin(TwoWire* i2c = nullptr);
		bool configure();
		bool setMTreg(byte MTreg);
		bool measurementReady(bool maxWait = false);
		float readLightLevel();

	private:
		
		byte _bhWrite(uint8_t d);
		byte _bhWrite2(uint8_t d1, uint8_t d2);
		bool _bhAck(byte ack);

		byte BH1750_MTreg = (byte)BH1750_DEFAULT_MTREG;
		// Correction factor used to calculate lux. Typical value is 1.2 but can
		// range from 0.96 to 1.44. See the data sheet (p.2, Measurement Accuracy)
		// for more information.
		const float BH1750_CONV_FACTOR = 1.2;
		TwoWire* I2C;
		unsigned long lastReadTimestamp;
};

#endif
