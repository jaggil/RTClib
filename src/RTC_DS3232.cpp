#include "RTClib.h"

#define DS3232_ADDRESS 0x68   ///< I2C address for DS3232
#define DS3232_TIME 0x00      ///< Time register
#define DS3232_ALARM1 0x07    ///< Alarm 1 register
#define DS3232_ALARM2 0x0B    ///< Alarm 2 register
#define DS3232_CONTROL 0x0E   ///< Control register
#define DS3232_STATUSREG 0x0F ///< Status register
#define DS3232_TEMPERATUREREG                                                  \
  0x11 ///< Temperature register (high byte - low byte is at 0x12), 10-bit
///< temperature value
#define DS3232_NVRAM 0x14 ///< Start of RAM registers - 236 bytes, 0x14 to 0xEC
/**************************************************************************/
/*!
        @brief  Start I2C for the DS3232 and test succesful connection
        @param  wireInstance pointer to the I2C bus
        @return True if Wire can find DS3232 or false otherwise.
*/
/**************************************************************************/
boolean RTC_DS3232::begin(TwoWire *wireInstance) {
  if (i2c_dev)
    delete i2c_dev;
  i2c_dev = new Adafruit_I2CDevice(DS3232_ADDRESS, wireInstance);
  if (!i2c_dev->begin())
    return false;
  return true;
}

/**************************************************************************/
/*!
        @brief  Check the status register Oscillator Stop Flag to see if the
   DS3232 stopped due to power loss
        @return True if the bit is set (oscillator stopped) or false if it is
   running
*/
/**************************************************************************/
bool RTC_DS3232::lostPower(void) {
  return read_register(DS3232_STATUSREG) >> 7;
}

/**************************************************************************/
/*!
        @brief  Set the date and flip the Oscillator Stop Flag
        @param dt DateTime object containing the date/time to set
*/
/**************************************************************************/
void RTC_DS3232::adjust(const DateTime &dt) {
  uint8_t buffer[8] = {DS3232_TIME,
                       bin2bcd(dt.second()),
                       bin2bcd(dt.minute()),
                       bin2bcd(dt.hour()),
                       bin2bcd(dowToDS3232(dt.dayOfTheWeek())),
                       bin2bcd(dt.day()),
                       bin2bcd(dt.month()),
                       bin2bcd(dt.year() - 2000U)};
  i2c_dev->write(buffer, 8);

  uint8_t statreg = read_register(DS3232_STATUSREG);
  statreg &= ~0x80; // flip OSF bit
  write_register(DS3232_STATUSREG, statreg);
}

/**************************************************************************/
/*!
        @brief  Get the current date/time
        @return DateTime object with the current date/time
*/
/**************************************************************************/
DateTime RTC_DS3232::now() {
  uint8_t buffer[7];
  buffer[0] = 0;
  i2c_dev->write_then_read(buffer, 1, buffer, 7);

  return DateTime(bcd2bin(buffer[6]) + 2000U, bcd2bin(buffer[5] & 0x7F),
                  bcd2bin(buffer[4]), bcd2bin(buffer[2]), bcd2bin(buffer[1]),
                  bcd2bin(buffer[0] & 0x7F));
}

/**************************************************************************/
/*!
        @brief  Read the SQW pin mode
        @return Pin mode, see Ds3232SqwPinMode enum
*/
/**************************************************************************/
Ds3232SqwPinMode RTC_DS3232::readSqwPinMode() {
  int mode;
  mode = read_register(DS3232_CONTROL) & 0x1C;
  if (mode & 0x04)
    mode = DS3232_OFF;
  return static_cast<Ds3232SqwPinMode>(mode);
}

/**************************************************************************/
/*!
        @brief  Set the SQW pin mode
        @param mode Desired mode, see Ds3232SqwPinMode enum
*/
/**************************************************************************/
void RTC_DS3232::writeSqwPinMode(Ds3232SqwPinMode mode) {
  uint8_t ctrl = read_register(DS3232_CONTROL);

  ctrl &= ~0x04; // turn off INTCON
  ctrl &= ~0x18; // set freq bits to 0

  write_register(DS3232_CONTROL, ctrl | mode);
}

/**************************************************************************/
/*!
        @brief  Get the current temperature from the DS3232's temperature sensor
        @return Current temperature (float)
*/
/**************************************************************************/
float RTC_DS3232::getTemperature() {
  uint8_t buffer[2] = {DS3232_TEMPERATUREREG, 0};
  i2c_dev->write_then_read(buffer, 1, buffer, 2);
  return (float)buffer[0] + (buffer[1] >> 6) * 0.25f;
}

/**************************************************************************/
/*!
        @brief  Set alarm 1 for DS3232
                @param 	dt DateTime object
                @param 	alarm_mode Desired mode, see Ds3232Alarm1Mode enum
        @return False if control register is not set, otherwise true
*/
/**************************************************************************/
bool RTC_DS3232::setAlarm1(const DateTime &dt, Ds3232Alarm1Mode alarm_mode) {
  uint8_t ctrl = read_register(DS3232_CONTROL);
  if (!(ctrl & 0x04)) {
    return false;
  }

  uint8_t A1M1 = (alarm_mode & 0x01) << 7; // Seconds bit 7.
  uint8_t A1M2 = (alarm_mode & 0x02) << 6; // Minutes bit 7.
  uint8_t A1M3 = (alarm_mode & 0x04) << 5; // Hour bit 7.
  uint8_t A1M4 = (alarm_mode & 0x08) << 4; // Day/Date bit 7.
  uint8_t DY_DT = (alarm_mode & 0x10)
                  << 2; // Day/Date bit 6. Date when 0, day of week when 1.
  uint8_t day = (DY_DT) ? dowToDS3232(dt.dayOfTheWeek()) : dt.day();

  uint8_t buffer[5] = {DS3232_ALARM1, uint8_t(bin2bcd(dt.second()) | A1M1),
                       uint8_t(bin2bcd(dt.minute()) | A1M2),
                       uint8_t(bin2bcd(dt.hour()) | A1M3),
                       uint8_t(bin2bcd(day) | A1M4 | DY_DT)};
  i2c_dev->write(buffer, 5);

  write_register(DS3232_CONTROL, ctrl | 0x01); // AI1E

  return true;
}

/**************************************************************************/
/*!
        @brief  Set alarm 2 for DS3232
                @param 	dt DateTime object
                @param 	alarm_mode Desired mode, see Ds3232Alarm2Mode enum
        @return False if control register is not set, otherwise true
*/
/**************************************************************************/
bool RTC_DS3232::setAlarm2(const DateTime &dt, Ds3232Alarm2Mode alarm_mode) {
  uint8_t ctrl = read_register(DS3232_CONTROL);
  if (!(ctrl & 0x04)) {
    return false;
  }

  uint8_t A2M2 = (alarm_mode & 0x01) << 7; // Minutes bit 7.
  uint8_t A2M3 = (alarm_mode & 0x02) << 6; // Hour bit 7.
  uint8_t A2M4 = (alarm_mode & 0x04) << 5; // Day/Date bit 7.
  uint8_t DY_DT = (alarm_mode & 0x08)
                  << 3; // Day/Date bit 6. Date when 0, day of week when 1.
  uint8_t day = (DY_DT) ? dowToDS3232(dt.dayOfTheWeek()) : dt.day();

  uint8_t buffer[4] = {DS3232_ALARM2, uint8_t(bin2bcd(dt.minute()) | A2M2),
                       uint8_t(bin2bcd(dt.hour()) | A2M3),
                       uint8_t(bin2bcd(day) | A2M4 | DY_DT)};
  i2c_dev->write(buffer, 4);

  write_register(DS3232_CONTROL, ctrl | 0x02); // AI2E

  return true;
}

/**************************************************************************/
/*!
        @brief  Disable alarm
                @param 	alarm_num Alarm number to disable
*/
/**************************************************************************/
void RTC_DS3232::disableAlarm(uint8_t alarm_num) {
  uint8_t ctrl = read_register(DS3232_CONTROL);
  ctrl &= ~(1 << (alarm_num - 1));
  write_register(DS3232_CONTROL, ctrl);
}

/**************************************************************************/
/*!
        @brief  Clear status of alarm
                @param 	alarm_num Alarm number to clear
*/
/**************************************************************************/
void RTC_DS3232::clearAlarm(uint8_t alarm_num) {
  uint8_t status = read_register(DS3232_STATUSREG);
  status &= ~(0x1 << (alarm_num - 1));
  write_register(DS3232_STATUSREG, status);
}

/**************************************************************************/
/*!
        @brief  Get status of alarm
                @param 	alarm_num Alarm number to check status of
                @return True if alarm has been fired otherwise false
*/
/**************************************************************************/
bool RTC_DS3232::alarmFired(uint8_t alarm_num) {
  return (read_register(DS3232_STATUSREG) >> (alarm_num - 1)) & 0x1;
}

/**************************************************************************/
/*!
        @brief  Enable 32KHz Output
        @details The 32kHz output is enabled by default. It requires an external
        pull-up resistor to function correctly
*/
/**************************************************************************/
void RTC_DS3232::enable32K(void) {
  uint8_t status = read_register(DS3232_STATUSREG);
  status |= (0x1 << 0x03);
  write_register(DS3232_STATUSREG, status);
}

/**************************************************************************/
/*!
        @brief  Disable 32KHz Output
*/
/**************************************************************************/
void RTC_DS3232::disable32K(void) {
  uint8_t status = read_register(DS3232_STATUSREG);
  status &= ~(0x1 << 0x03);
  write_register(DS3232_STATUSREG, status);
}

/**************************************************************************/
/*!
        @brief  Get status of 32KHz Output
        @return True if enabled otherwise false
*/
/**************************************************************************/
bool RTC_DS3232::isEnabled32K(void) {
  return (read_register(DS3232_STATUSREG) >> 0x03) & 0x01;
}
/**************************************************************************/

/**************************************************************************/
/*!
        @brief  Enable BB32KHZ Output
        @details The 32kHz output is enabled by default. It requires an external
        pull-up resistor to function correctly
*/
/**************************************************************************/
void RTC_DS3232::enableBB32KHZ(void) {
  uint8_t status = read_register(DS3232_STATUSREG);
  status |= (0x1 << 0x06);
  write_register(DS3232_STATUSREG, status);
}

/**************************************************************************/
/*!
        @brief  Disable BB32KHZ Output
*/
/**************************************************************************/
void RTC_DS3232::disableBB32KHZ(void) {
  uint8_t status = read_register(DS3232_STATUSREG);
  status &= ~(0x1 << 0x06);
  write_register(DS3232_STATUSREG, status);
}

/**************************************************************************/
/*!
        @brief  Get status of BB32KHZ Output
        @return True if enabled otherwise false
*/
/**************************************************************************/
bool RTC_DS3232::isEnabledBB32KHZ(void) {
  return (read_register(DS3232_STATUSREG) >> 0x06) & 0x01;
}

/**************************************************************************/
/*!
        @brief  Clear Oscillator Stop Flag (OSF). Bit 7 of STATUSREG (0Fh)
        @details A logic 1 in this bit indicates that the oscillator either is
         stopped or was stopped for some period and may be used to judge the
   validity of the timekeeping data. This bit is set to logic 1 any time that
   the oscillator stops. The following are examples of conditions that can cause
   the OSF bit to be set: 1) The first time power is applied. 2) The voltages
         present on both VCC and VBAT are insufficient to support oscillation.
   3) The EOSC bit is turned off in battery-backed mode. 4) External influences
   on the crystal (i.e., noise, leakage, etc.). This bit remains at logic 1
   until written to logic 0.
*/
/**************************************************************************/
void RTC_DS3232::clearOSF(void) {

  uint8_t statreg = read_register(DS3232_STATUSREG);
  statreg &= ~0x80; // flip OSF bit
  write_register(DS3232_STATUSREG, statreg);
}
/**************************************************************************/
/*!
        @brief  Enable EOSF. Enable Oscillator (EOSC) Bit 7 of Control Register
   (0Eh)
        @details When set to logic 0, the oscillator is started (inverted
   logic). When set to logic 1, the oscillator is stopped when the DS3232
   switches to battery power. This bit is clear (logic 0) when power is first
   applied. When the DS3232 is powered by VCC, the oscillator is always on
   regardless of the status of the EOSC bit. When EOSC is disabled, all register
   data is static.
*/
/**************************************************************************/
void RTC_DS3232::enableEOSC(void) {
  uint8_t status = read_register(DS3232_CONTROL);
  status &= ~(0x1 << 0x07);
  write_register(DS3232_CONTROL, status);
}
/**************************************************************************/
/*!
        @brief  Disable EOSF. When set to logic 1, the oscillator is stopped
   (inverted logic)
*/
/**************************************************************************/
void RTC_DS3232::disableEOSC(void) {
  uint8_t status = read_register(DS3232_CONTROL);
  status |= (0x1 << 0x07);
  write_register(DS3232_CONTROL, status);
}
/**************************************************************************/
/*!
        @brief  Get status of EOSF
        @return When set to logic 0, the oscillator is started (inverted logic)
*/
/**************************************************************************/
bool RTC_DS3232::isEnabledEOSC(void) {
  return (read_register(DS3232_CONTROL) >> 0x07) & 0x01;
}

/**************************************************************************/
/*!
        @brief  Read data from the DS3232's NVRAM
        @param buf Pointer to a buffer to store the data - make sure it's large
   enough to hold size bytes
        @param size Number of bytes to read
        @param address Starting NVRAM address, from 0 to 236
*/
/**************************************************************************/
void RTC_DS3232::readnvram(uint8_t *buf, uint8_t size, uint8_t address) {
  uint8_t addrByte = DS3232_NVRAM + address;
  i2c_dev->write_then_read(&addrByte, 1, buf, size);
}
/**************************************************************************/
/*!
        @brief  Write data to the DS3232 NVRAM
        @param address Starting NVRAM address, from 0 to 236
        @param buf Pointer to buffer containing the data to write
        @param size Number of bytes in buf to write to NVRAM
*/
/**************************************************************************/
void RTC_DS3232::writenvram(uint8_t address, const uint8_t *buf, uint8_t size) {
  uint8_t addrByte = DS3232_NVRAM + address;
  i2c_dev->write(buf, size, true, &addrByte, 1);
}

/**************************************************************************/
/*!
        @brief  Shortcut to read one byte from NVRAM
        @param address NVRAM address, 0 to 236
        @return The byte read from NVRAM
*/
/**************************************************************************/
uint8_t RTC_DS3232::readnvram(uint8_t address) {
  uint8_t data;
  readnvram(&data, 1, address);
  return data;
}

/**************************************************************************/
/*!
        @brief  Shortcut to write one byte to NVRAM
        @param address NVRAM address, 0 to 236
        @param data One byte to write
*/
/**************************************************************************/
void RTC_DS3232::writenvram(uint8_t address, uint8_t data) {
  writenvram(address, &data, 1);
}
