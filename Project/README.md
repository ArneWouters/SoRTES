# Time-Synchronized Embedded Device
## Step 0
 - End-Device (ED) Listens
   - for first beacon
   - for user commands on serial port
 - If user sends command
   - ED executes user command
 - Gateway (GW) sends beacon
   - ED receives beacon

## Step 1
 - ED Parses beacon for next interval
 - ED reads from temperature sensor
 - ED writes values to EEPROM
 - ED transmits temperature value to GW
 - ED sleeps until next beacon

## Step 2
 - ED wakes up to receive next beacon
 - GW sends next beacon
 - ED receives next beacon
   - if less than 20 beacons received
     - then Step 1
   - else Step 3

## Step 3
 - ED goes to ultra low-power mode
 - GW prints metadata to serial port
