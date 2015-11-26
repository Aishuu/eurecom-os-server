# EURECOM OS Contest Server

A simple server that enables communication between NXT and EV3 Lego Mindstorm.

It is first intended to be used in the OS course of EURECOM but part or whole of it can be freely reused by anyone.

## Setup

### For EV3

Download the kernel image `eurecom-ev3dev.img.xz`. Follow the steps from [http://www.ev3dev.org/docs/getting-started/](http://www.ev3dev.org/docs/getting-started/) with the provided kernel image. Root account was given the password "eurecom".

If you do not have blueman, set it up as described: [http://www.ev3dev.org/docs/tutorials/connecting-to-the-internet-via-bluetooth/](http://www.ev3dev.org/docs/tutorials/connecting-to-the-internet-via-bluetooth/).

Turn on the EV3. Unpair the device on the server. Pair again from the server: enter PIN code and reenter it on EV3. Connect by SSH on the EV3. On both the server and the EV3 the command
```
$ hcitool scan
```

should show the connection.

When the server is up and the game has started you can run the program (`client/NXT/client.c` for instance).

### For NXT

In order to connect from a linux laptop to an NXT you will need to follow the steps given in [http://mtc.epfl.ch/courses/ProblemSolving-2007/nxt.html](http://mtc.epfl.ch/courses/ProblemSolving-2007/nxt.html). Note that these steps are getting old and there might be some discrepencies with the behaviour you will observe.

In particular `bluez-utils` has been replaced with the `bluez` package and sdptool has compatibility issues with recent versions of the bluetooth daemon (on Ubuntu in particular). In this case the command
```
$ sdptool add --channel=3 SP
```
will return -1 with no other error message. This command is used to advertise the Serial Port service to the NXT brick and register it to a specific local channel. Without it the NXT brick will abort the connexion when it sees that no Serial Port service is available (this is the only profile supported by the NXT) and thus a "Line is busy" should be displayed on the brick's screen.

In order for this command to work, the bluetooth daemon should be restarted with the `-C` option. Either by hand:
```
$ /etc/init.d/bluetooth stop
$ bluetoothd -C
```
or by modifying /etc/init.d/bluetooth to add the `-C` option (TODO: what exactly to modify?).

Note that in most recent distributions the bluez will be present and enabled by default so you don't need to do steps 1 and 2. However you do need to pair the laptop with the NXT brick before trying to connect:
* Enable bluetooth on the NXT and make it visible
* From the bluetooth applet of your desktop manager on your laptop, scan for visible device
* Try to pair with the NXT
* A prompt on the brick's screen should enable you to choose a PIN code (1234 by default)
* Enter the PIN code on the laptop

Then add the Serial Port service to advertised service by running
```
$ sdptool add --channel=1 SP
```
Note that the server uses channel 1 by default. You can then run the server, launch a game and connect the NXT to the server by
navigating in the bluetooth menu. You may see an error message "Line is busy" when you try to connect but it should disappear if
you insist a bit. Then run your client program (as provided as example).


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
