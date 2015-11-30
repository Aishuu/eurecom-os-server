# EURECOM OS Contest Server

A simple server that enables communication between NXT and EV3 Lego Mindstorm.

It is first intended to be used in the OS course of EURECOM but part or whole of it can be freely reused by anyone.

## Setup

### For EV3

Follow the steps from [http://www.ev3dev.org/docs/getting-started/](http://www.ev3dev.org/docs/getting-started/) to download the ev3dev kernel image and flash it on the SD card.

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
or by modifying /etc/init.d/bluetooth to add the `-C` option.

Note that in most recent distributions the bluez will be present and enabled by default so you don't need to do steps 1 and 2 of the referenced tutorial. However you do need to pair the laptop with the NXT brick before trying to connect:
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

You can compile the server using the provided Makefile. If you run into an error stating that `bluetooth/bluetooth.h` was not found,
you will need to install the `libbluetooth-dev` package (on Debian-like UNIX).

Run the server with an appropriate team file. Each line of the team file should be formatted as:
```
[TYPE] [ADDR] [NAME]
```
where:
* `[TYPE]` : `1` for NXT, `2` for EV3
* `[ADDR]` : in the form `aa:bb:cc:dd:ee:ff` is the bluetooth address
* `[NAME]` : is the name of the team

You can then run the server as
```
$ ./server teams
```
or
```
$ ./server teams log
```
if you wish to log the session.

## Protocol

To communicate between each other, NXT and EV3 must comply with the specified protocol. Invalid messages will be discarded by the
server so robots can always consider that received messages are well formatted (except for `CUSTOM` messages, whose structure is
not pre-determined).

Each message consists of a header and a body. Note that all numbers are unsigned integers whose formats are little-endian.

The protocol may evolve according to needs and proposals.

### Header

The header is 5-bytes long:

```
   0      1      2      3      4
+------+------+------+------+------+
|     ID      | src  | dst  | type |
+------+------+------+------+------+
```

Fields description:
* `ID` is a 2-bytes number identifying the message (kind of like a sequence number). It is used when acknowledging messages.
* `src` is a 1-byte number identifying the team who sent the message (it is unique for the whole contest).
* `dst` is a 1-byte number identifying the team who should receive the message (it is unique for the whole contest).
* `type` is a 1-byte code that identify the kind of message that is sent.

### Body

#### ACTION

ACTION messages are used to advertise an intended movement. They are 10-bytes long:
```
    0       1       2       3       4       5       6       7       8       9
+-------+-------+-------+-------+-------+-------+-------+-------+-------+-------+
|      ID       |  src  |  dst  |   0   |     angle     | dist  |     speed     |
+-------+-------+-------+-------+-------+-------+-------+-------+-------+-------+
```

Fields description:
* `angle` is a 2-bytes number representing the planned direction (in degree). Values over 360 have undefined meaning.
* `dist` is a 1-byte number representing the planned distance (in cm).
* `speed` is a 2-bytes number representing the planned speed (in mm/s).

#### ACK

ACK messages are used to acknowledge the reception of messages. They are 8-bytes long:
```
    0       1       2       3       4       5       6       7
+-------+-------+-------+-------+-------+-------+-------+-------+
|      ID       |  src  |  dst  |   1   |    ID ack     | state |
+-------+-------+-------+-------+-------+-------+-------+-------+
```

Fields description:
* `ID ack` is the ID of the message that is acknowledged
* `state` is a status code. `0 -> OK`, `1 -> error`. Other status codes may be used for acknowledging custom messages.

START and STOP messages should not be acknowledged.

#### LEAD

LEAD messages should only be used by the current leader in order to transfer leadership to the next robot. They are 5-bytes long:
```
    0       1       2       3       4
+-------+-------+-------+-------+-------+
|      ID       |  src  |  dst  |   2   |
+-------+-------+-------+-------+-------+
```

#### START

START messages can only be used by the server. One is sent to each team when the game starts. If the robot disconnect and reconnect
during the game, another START message will be sent to it right after it connects to the server. They are 9-bytes long:
```
    0       1       2       3       4       5       6       7       8
+-------+-------+-------+-------+-------+-------+-------+-------+-------+
|      ID       |  src  |  dst  |   3   | rank  | size  | prev  | next  |
+-------+-------+-------+-------+-------+-------+-------+-------+-------+
```

Fields description:
* `rank` is the rank of the robot in the snake. It can be anything from `0` for the leader to `size-1` for the last robot.
* `size` is the length of the snake, that is, how many robots are participating in this game.
* `prev` is the ID of the previous robot in the snake. If there is no previous robot it will be `0xFF`.
* `next` is the ID of the next robot in the snake. If there is no next robot it will be `0xFF`.

#### STOP

STOP messages are sent by server to every robot when the game ends. They are 5-bytes long:
```
    0       1       2       3       4
+-------+-------+-------+-------+-------+
|      ID       |  src  |  dst  |   4   |
+-------+-------+-------+-------+-------+
```

#### WAIT

WAIT messages can be sent to the previous robot to request for a halt. It is up to the receiving robot to answer or ignore them.
WAIT messages are 6-bytes long.
```
    0       1       2       3       4       5
+-------+-------+-------+-------+-------+-------+
|      ID       |  src  |  dst  |   5   | delay |
+-------+-------+-------+-------+-------+-------+
```

Fields description:
* `delay` is the reqested halt time (in s).

#### CUSTOM

CUSTOM messages may be used to increment the protocol if teams wish to add their own custom messages. CUSTOM messages can not have
a size greater than 58 bytes (header included).
```
    0       1       2       3       4        5      . . .
+-------+-------+-------+-------+-------+-----------------
|      ID       |  src  |  dst  |   6   |   payload . . .
+-------+-------+-------+-------+-------+-----------------
```
