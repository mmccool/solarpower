# Solar Power Monitor
Interfaces with three INA219s, a DHT12, and a BMP280 to monitor
power flows, charge status, and environmental conditions for a solar
power system (panels and battery).

The `solarpower.ino` file should be installed on an M5Stack connected
via USB serial to a host computer.  Optionally, the `solarpower.js`
program should be run as a service (autostarting with `solarpower.service`)
to create a web service interface.
