(This document and the accompanying source code are copies of material
originally made available [here][source].)

[source]: http://egirland.blogspot.com/2014/03/arduino-uno-as-usb-to-gpib-controller.html

#  ARDUINO UNO as a USB to GPIB controller

## Disclaimer

This program is provided as is. It is a hobby work.  It doesn't work
correctly. It has not been tested. It can damage your Arduino and the
device you connect it to.  Can have unexpected behaviors, can get
stuck at any moment, can read and write data to/from the device in
ways you might not expect. It can send wrong or erratic commands to
the device. Can display data that differ from the actual data the
device sent out. Can address GPIB devices differently from what you
might expect.  I have not conducted any speed test; the maximum speed
supported is simply unknown.  Is does not follow any official
standard. Only a minimum set of the functions included in IEEE-488 is
barely emulated.  Hardware limitations: the lack of a GPIB line driver
has two major implications: first: your Arduino is directly connected
to the device GPIB port without any form of electrical protection of
buffering - this can potentially damage your Arduino and/or your
device; second: what can happen if you connect more than one GPIB
device to the BUS is unpredictable both from an hardware and software
point of view. I only experimented with a single GPIB device connected
to the BUS.

##  Introduction

This project is aimed to provide a cheap and quick GPIB solution for those that need to gain control over a single instrument and interact with it (e.g. to dumo screen shots or calibrate instruments that can be calibrated via GPIB only -- HP6632A Power Supply being a good example).

I supposed that cheap adapters were available on the market to interface GPIB instruments with a PC. I was wrong. The only feasible solution is to go with a Prologix USB to GPIB converter, still it would have cost me more than 100 EUR .
Other solutions (open or proprietary) required buying or building hardware.

On the other end I had an Arduino UNO floating around on my bench waiting for a problem to solve other than blinking leds or writing "hello world" on an LCD..

So I decided to face the challenge of writing down a sketch and have
the Arduino play the adapter role I was looking for.  To make a long
story short.. (I supposed that GPIB was simple at least as much RS-232
is, .. and it is not! I was wrong again!) after reading a lot of
IEEE-488 documentation, I got a decent implementation of a GPIB
controller out of my Arduino UNO. This post is dedicated to describe
the project.

##  Code availability

I some of the feedbacks post in the HP or TEK Yahoo discussion groups
I was accused of not having published the source code. Here it is.
Enjoy it! BUT: I am interested in test instruments. I am not
interested in developing open source projects. I wrote this program
just because I am an ICT professional and consequently I have some
educational reminiscence of programming.

Because of the hard effort I put on the project, I kindly request you
to donate a couple of bucks if you find this program useful for your
hobby (see the [original page][source] for the donation link).

---

This program allows to have the Arduino UNO to implement a GPIB
controller. The objective was to have a way to get control of test
instruments by the ability to send and receive commands to/from such
devices via GPIB using a terminal emulator or an appropriate script.

Despite the disclamer I can say that the objective has been
successfully met; with this program uploaded to an Arduino UNO I can
now create sessions with my hobby instruments and interact with them
the way they allow too do (see instrument manual).  Most of the
version 1 limitations are still here. Actual limitations are:

- the program operates in controller mode only; it is not (yet) able to behave like a device.
- two important GPIB pins are not managed: REN and SRQ;
- Serial poll is not implemented.
- subaddressing is not implemented.
- ++read [char] not implemented (useful?!);
- ++rst, ++lon, ++savecfg, ++spoll, ++srq, ++status, ++help are not implemented;

##  Hardware set up instructions:

The hardware needed is simple: just a bunch of wires from the GPIB plug to Arduino pins. Here is the pin mapping:

    A0 GPIB 1
    A1 GPIB 2
    A2 GPIB 3
    A3 GPIB 4
    A4 GPIB 13
    A5 GPIB 14
    4  GPIB 15
    5  GPIB 16
    12 GPIB 5
    11 GPIB 6
    10 GPIB 7
    9  GPIB 8
    8  GPIB 9
    7  GPIB 11

**NOTE**: GPIB pins 10, 17-24 goto GND, but (!!):

- GPIB pin 17 (REN) has to be implemented ..stay ready to reroute it differently in the future;
- GPIB pin 10 (SRQ) has to be implemented ..stay ready to reroute it differently in the future;
- GPIB pin 12 should be connected to the cable shield (not used here - I left it n/c).

Build the the shield the way you want. Nothing is simpler provided you
have a minimum electronic construction skills.  For testing I
preferred the "flying" shield visible in the pictures but the actual
construction is your choice.  The male GPIB connector was realized
sacrificing a CENTRONICS 32pins male connector out of a PC parallel
printer cable. I have just cut it leaving 12 positions out of the
original 16.

####  Get it working

Get the GPIB6.1.ino sketch compile it and upload it to the Arduino.
If you want/need to see a bunch of debugging output, just uncomment
the two #defines directives on top of the source file.

Get any terminal emulator and manage to have it working with the FTDI
RS232/USB converter (nothing to do under linux; install the FTDI
driver in Windows: it cames for free with the Arduino IDE. I have
successfully used the serial display provided by the arduino IDE,
putty under Linux and HyperTerminal under (virtual) Windows XP.

You probably have to set the some serial communication parameters.
The default serial speed is 115200 bps.  Other serial parameters are:

- Data_bits=8
- Stop_bits=1
- Parity=None
- Flow_control=None.

I successfully also interacted with the controller using cat, tail -f,
echo, .. linux commands redirected to the FTDI serial device
(/dev/ttyACM0 in my case). To do that you have to play a little with
the stty command. For sure you need to set -hupcl so that the DTR 232
signal is not deasserted while disconnecting from the device. It can
also be useful to defeat the "reboot on connection" feature of the
Arduino. Google about these topics before doing it! You can make your
arduino impossible to reprogram.

The controller is silent at startup. To test it send a "++ver" command
and the Arduino should reply with "ARDUINO GPIB firmware by E.
Girlando Version 6.1".  If you plan to interact directly with your
instrument, please issue the command "++verbose". This will make your
controller much more friendly (see below).

##  Principle of operations

Everything received from the USB is passed to GPIB (126 char max).

Characters from the USB are fetched in line by line and buffered. The software support for both CR, LF and CRLF line termination.

Once the line termination char is encountered in the USB input stream
the entire buffer gets parsed. If you need to include CR or LF in the
input stream to be sent to your instrument you must rely on the help
of the linux serial device driver using ^V in front of ^M or ^J to
defeat their default behaviour of terminating the command shell
lines.. (see stty man page). Still the ^V mechanism is not enough. ^V
prevents the linux driver from sending out your line in the middle of
your editing, but still the received CR or LF are interpreted as end
of line by the Arduino. To avoid this an escape mechanism has been
implemented. The escape char is "ESC" or 0x1B. So to send CR to the
GPIB BUS you need to type "ESC^V^M" and similarly for LF. To send a
single "ESC" you must escape it with "ESC" so you need to type "ESC"
two times. You can terminate the input line with "" so that the
following CR or LF will be escaped. If you insert unescaped CR or LF
in the stream (e.g typing "^V^M" with no preceding "ESC") the line is
split in two parts: everything preceding the CR or LF is passed on for
processing; everything following the CR or LF is simply discarded.
I don't know of any way to send CR or LF or ESC ascii char from the Arduino IDE serial monitor. Testing for all this stuff has been conducted via putty.

**WARNING**: if you use buffered I/O on the sender side, and you often do,
be aware that sending more than 64 chars per line can lead to serial
buffer overflow as the Arduino sketch cannot keep pace with the burst
of characters coming in @115200bps. Yet, the program detects this
situation, issues a "Serial buffer overflow - line discarded. Try
setting non buffered I/O" error message and discards the line
entirely. As the error message suggests, you can switch your sender
(terminal emulator) to unbuffered I/O so that it sends out one char at
a time. If you are typing at a keyboard the problem is solved (unless
you are typing at the speed of light); if the data comes from some
kind of script, care must be taken to send small chunks of data
introducing delays to allow for the program to eat them.

Line starting with "++" are assumed to be commands directed to the
controller and are parsed as such. Nothing beginning with "++" can be
sent to instruments.  Everything not starting with "++" is sent to the
addressed instrument over the GPIB BUS.

##  Implemented commands

**NOTE**: the command interpreter is very raw: I remember from high school that they have to be programmed in a certain way, but I don't really remember how; so I did my best. Misspelled commands will (better: should) generate a Syntax error message. As soon as a non existent command code or a Syntax error is detected the line is discarded entirely. After a valid command has been recognized, any additional characters following it (if any) is discarded.


- **++addr [address]**
Fully compatible with the "++" de facto standard, but subaddressing is not implemented (SADs are syntactically accepted but ignored).

- **++ver**
just displays a version string of the controller.

- **++auto**
Fully compatible with the "++" de facto standard.
But what is automode? Ok, you have to know that GPIB devices do not work the way we are used to with modern devices. When you send a command they process it and if any output is generated they put it to an instrnal buffer waiting to be addressed to speak. The final result is that just to get the identification string out of a device you have to send the request command (e.g "id?") and then issue the command "++read" to the controller. Automode ON just makes this for you: for every string sent to GPIB, the controller issues an implicit "++read" to the addressed device. This gives you the impression to have the device immediately react in front of the string you sent it. Beautiful! ...however a hidden trap is around: some GPIB device gets upset if asked to speak when it has nothing to say and with automode ON this can happen very often (I have an instrument that lights the error annunciator on the front panel when this situation occurs, but yet it continues working as nothing happened).
For this reason automode is blocked if you just send an empty string pressing CR or NL. Automode is also suppressed with "++" commands.

- **++clr**
Fully compatible with the "++" de facto standard.

- **++eoi**
Fully compatible with the "++" de facto standard.

- **++eos**
Fully compatible with the "++" de facto standard.

- **++eot_enable**
Fully compatible with the "++" de facto standard.

- **++eot_char**
Fully compatible with the "++" de facto standard.

- **++ifc**
Fully compatible with the "++" de facto standard.

- **++llo**
Fully compatible with the "++" de facto standard.

- **++loc**
Fully compatible with the "++" de facto standard.

- **++mode**
the command is syntactically fully compatible, but the only mode implemented is CONTROLLER; so the cammand is provided for compatibility but it is inoperative.


- **++read**
Fully compatible with the "++" de facto standard, but ++read [char] is not implemented.

- **++read_tmo_ms**
Fully compatible with the "++" de facto standard.

- **++trg**
is implemented for the addressed device only; it doesn't accept PADs or SADs paramenters.


- **++rst, ++lon, ++savecfg, ++spoll, ++srq, ++status, ++help** are not implemented.

Two additional proprietary commands (not usually included in the "++" set) have been implemented:

- **++verbose**
This command toggles the controller between verbose and silent mode. When in verbose mode the controller assumes an human is working on the session: a "&gt;" prompt is issued to the USB side of the session every time the controller is ready to accept a new command and most of the "++" commands now print a useful feedback answers (i.g. the new set value or error messages).
This breaks the full "++" compatibility. Do not use verbose mode when trying to use third party software. Default is verbose OFF.
This is very useful to see timeout errors otherwise hidden..

- **++dcl**
Sends an unaddressed DCL (Universal Device Clear) command to the GPIB. It is a message to the instrument rather than to its interface. So it is left to the instrument how to react on a DCL. Typically it generates a power on reset. See instrument documentation.

##  Feedbacks

This version can be successfully used to interact with your
instruments, but is still imperfect. Please post feedbacks and
comments below this post.  In addiction to bug fixing, I am interested
in sharing experiences in using third party software especially for
dumping plots down from instruments. I have a TEK2232 and an HP853 I
am interested in producing plots from.

##  Future development

I am going to improve it by bug fixing and keep the documentation deep and updated and working on Version 6.2 that will wide the project scope up to:

- settings save/restore,
- serial poll support (if not too difficult),
- improve ++trg compatibility,
- porting to different Arduino platforms.

##  Conclusions

Enjoy you GPIB controller and please do not forget to donate some buck just to recognize the effort I made to develop and document this project.
Thank you!
Emanuele.

## License

![Creative Commons License][3]
Arduino USB to GPIB firmware by [E. Girlando][4] is licensed under a [Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License][5].
Permissions beyond the scope of this license may be available at [mailto:emanuele_girlando@yahoo.com][6].

[1]: https://3.bp.blogspot.com/-wE96mGo60aw/UxO-URJXFfI/AAAAAAAAALY/rn5Y8xF-vSI/s1600/DSCN0065.JPG "TEK2232 loves Arduino"
[2]: https://1.bp.blogspot.com/-39JcmqnoDZY/UxO-VGooaJI/AAAAAAAAALg/8OZBcEPSPoo/s1600/DSCN0066.JPG "Fluke PM2534 loves Arduino"
[3]: http://i.creativecommons.org/l/by-nc-nd/4.0/88x31.png
[4]: http://egirland.blogspot.it/
[5]: http://creativecommons.org/licenses/by-nc-nd/4.0/
[6]: mailto:emanuele_girlando@yahoo.com
[7]: http://egirland.blogspot.com/2014/02/arduino-uno-as-usb-to-gpib-adapter.html
[8]: https://3.bp.blogspot.com/-peAxLPToKwE/UvH1nqcAogI/AAAAAAAAAKk/QvRzaeImYaU/s1600/DSCN0057.JPG "GPIB-Arduino fixture"
