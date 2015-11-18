# EURECOM OS Contest Server

A simple server that enables communication between NXT and EV3 Lego Mindstorm.

It is first intended to be used in the OS course of EURECOM but part or whole of it can be freely reused by anyone.

## Setup

### For EV3

Turn on the EV3. Unpair the device on the server. Pair again from the server: enter PIN code and reenter it on EV3. Connect by SSH on the EV3. On both the server and the EV3 the command
```
$ hcitool scan
```

should show the connection.

When the server is up and the game has started you can run the program (rfcomm-client for instance).

### For NXT

TODO

### For the server

Run the server with an appropriate team file.

## Notes

### Fonts

I used this website [http://patorjk.com/software/taag/](http://patorjk.com/software/taag/)

Other readable fonts are:
* big
* doom
* fire font-s
* Graceful
* Graffiti
* Rectangles
* Slant
* Small
* Small Slant
* Standard
