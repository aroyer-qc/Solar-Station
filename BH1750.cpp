/*
  This is a library for the BH1750 Digital Light Sensor.

  The BH1750 board uses I2C for communication. Two pins are required to
  interface to the device. Configuring the I2C bus is expected to be done
  in user code.
  this reduce version only use 
*/

#include "BH1750.h"

/**
 * Constructor
 * @params None
 *
 * On most sensor boards, it was 0x76
 */
BH1750::BH1750()
{
    // Allows user to change TwoWire instance
    I2C = &Wire;
}

/**
 * Configure sensor
 * @param mode Measurement mode
 * @param addr Address of the sensor
 * @param i2c TwoWire instance connected to I2C bus
 */
bool BH1750::begin(TwoWire* i2c)
{
    bool State;

    // I2C is expected to be initialized outside this library
    // But, allows a different address and TwoWire instance to be used
    if(i2c != nullptr)
    {
        I2C = i2c;
    }

    State = (configure() && setMTreg(BH1750_DEFAULT_MTREG));

    if(State == true)
    {
      #ifdef USE_DEBUG_1750  
        Serial.println("[BH1750] Init OK.");
      #endif    
    }
    else
    {
      #ifdef USE_DEBUG_1750  
        Serial.println("[BH1750] Init failed.");
      #endif    
    }

	// Configure sensor in specified mode and set default MTreg
	return(State);
}

/**
 * Configure BH1750 with specified mode
 * @param mode Measurement mode
 */
bool BH1750::configure()
{
    byte ack = _bhWrite((uint8_t)BH1750_CONTINUOUS_HIGH_RES_MODE);

    delay(10);

    if(_bhAck(ack))
    {
        lastReadTimestamp = millis();
        return true;
    }

    return false;
}

/**
 * Configure BH1750 MTreg value
 * MT reg = Measurement Time register
 * @param MTreg a value between 31 and 254. Default: 69
 * @return bool true if MTReg successful set
 *      false if MTreg not changed or parameter out of range
 */
bool BH1750::setMTreg(byte MTreg)
{
    if(MTreg < BH1750_MTREG_MIN || MTreg > BH1750_MTREG_MAX)
    {
      #ifdef USE_DEBUG_1750
        Serial.println(F("[BH1750] ERROR: MTreg out of range"));
      #endif
        return false;
    }
    
    byte d1 = (0b01000 << 3) | (MTreg >> 5);
    byte d2 = (0b011 << 5) | (MTreg & 0b11111);

    byte ack = _bhWrite2(d1, d2);
    ack |= _bhWrite(BH1750_CONTINUOUS_HIGH_RES_MODE);
    delay(10);

    if(_bhAck(ack))
    {
        BH1750_MTreg = MTreg;
        return true;
    }

    return false;
}

/**
 * Checks whether enough time has gone to read a new value
 * @param maxWait a boolean if to wait for typical or maximum delay
 * @return a boolean if a new measurement is possible
 *
 */
bool BH1750::measurementReady(bool maxWait)
{
	unsigned long delaytime = 0;
    maxWait ? delaytime = (180 * BH1750_MTreg / (byte)BH1750_DEFAULT_MTREG)
            : delaytime = (120 * BH1750_MTreg / (byte)BH1750_DEFAULT_MTREG);

	unsigned long currentTimestamp = millis();
	
	if((currentTimestamp - lastReadTimestamp) >= delaytime)
	{
		return true;
	}

    return false;
}

/**
 * Read light level from sensor
 * The return value range differs if the MTreg value is changed. The global
 * maximum value is noted in the square brackets.
 * @return Light level in lux (0.0 ~ 54612,5 [117758,203])
 *     -1 : no valid return value
 *     -2 : sensor not configured
 */
float BH1750::readLightLevel()
{
	float level = -1.0;

	if(I2C->requestFrom((int)BH1750_I2C_ADDRESS, (int)2) == 2)
	{
		uint32_t tmp = 0;
		tmp = I2C->read();
		tmp <<= 8;
		tmp |= I2C->read();
		level = tmp;
	}
  
	lastReadTimestamp = millis();

	if(level != -1.0)
	{
      #ifdef BH1750_DEBUG
		Serial.print(F("[BH1750] Raw value: "));
		Serial.println(level);
	  #endif

		if(BH1750_MTreg != BH1750_DEFAULT_MTREG)
		{
			level *= (float)((byte)BH1750_DEFAULT_MTREG / (float)BH1750_MTreg);
		  #ifdef BH1750_DEBUG
			Serial.print(F("[BH1750] MTreg factor: "));
			Serial.println(
			String((float)((byte)BH1750_DEFAULT_MTREG / (float)BH1750_MTreg)));
		  #endif
		}
    
		level /= BH1750_CONV_FACTOR;

	  #ifdef BH1750_DEBUG
        Serial.print(F("[BH1750] Converted float value: "));
        Serial.println(level);
      #endif
    }

    return level;
}

//--------------------------------------------------------------------------
// PRIVATE HELPERS (formatage intact)
//--------------------------------------------------------------------------

byte BH1750::_bhWrite(uint8_t d)
{
    I2C->beginTransmission(BH1750_I2C_ADDRESS);
    I2C->write(d);
    return I2C->endTransmission();
}

byte BH1750::_bhWrite2(uint8_t d1, uint8_t d2)
{
    byte ack = _bhWrite(d1);
    ack |= _bhWrite(d2);
    return ack;
}

bool BH1750::_bhAck(byte ack)
{
    if(ack == 0) return true;

  #ifdef USE_DEBUG_1750
    switch(ack)
    {
        case 1:  Serial.println(F("[BH1750] ERROR: too long for transmit buffer")); 		break;
        case 2:  Serial.println(F("[BH1750] ERROR: received NACK on transmit of address")); break;
        case 3:  Serial.println(F("[BH1750] ERROR: received NACK on transmit of data")); 	break;
        case 4:  Serial.println(F("[BH1750] ERROR: other error")); 							break;
        default: Serial.println(F("[BH1750] ERROR: undefined error")); 						break;
    }
  #endif

    return false;
}
