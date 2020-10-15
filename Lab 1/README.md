# Exercises
## Exercise 1 :heavy_check_mark:
- Display the following in serial port (console), from your Arduino application
  - > Enter LED status (on/off):
- Read the user input value from serial port into your application
- If value is `"on"`, display the following in serial port from your Arduino application
  - > Enter the blink rate (1-60 sec):
- Print user provided values to serial port
  - > You have selected LED on/off.  Blink rate is xx sec.
- After the user selected values are displayed in serial port, the blink pattern should be seen on the onboard LED
  - Info: Use a timer library to set the blink rate

## Exercise 2 :heavy_check_mark:
- Create a timestamp module (A simple counter should be enough
- Create a small database (You may use Arduino library)
- Calibrate and read the temperature value from Arduino and store it into the database
- Power off and on the board, your database must be maintained without being deleted. (Look for Arduino EEPROM module)

## Exercise 3 :construction:
- Instead of database from the previous exercise, use a linked list for storage
- The linked list must be persisted across board power on/off cycle.
- If you need to refresh pointers in C, you can check this out:
  - https://www.learn-c.org/en/Welcome
