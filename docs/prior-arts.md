# Standing on the shoulders of giants

### Sports computer system (1969)
Loop antenna is emitting a signal continously (near field); transponder in the car receives it, and emits a new signal specific to individual racer.

There is schematics included for the transponder's transmitting section. Might worth an LTSpice simulation once.

https://patents.google.com/patent/US3546696A/en

### Race calling system (1973)

> The system includes a plurality of pickup loops stationed at call points around the track, which loops coact with transmitters carried by the several entries

Has a simplified transponder schematics

https://patents.google.com/patent/US3795907A/en

Same:
https://patents.google.com/patent/US5696481A/en - Process for recording intermediate and final times in sporting events (1991), passive transponder
https://patents.google.com/patent/US4999604A/en (1989)

### Timing apparatus and system (1974)

> antenna type loops are situated at predetermined positions about a race track in which a plurality of contestants are passed sequentially over the loops while carrying a transmitter arranged to transmit a low radio frequency signal 

Transmitter simplifies schematics available

https://patents.google.com/patent/US3946312A/en

Similar:
https://patents.google.com/patent/US3943339A/en (1974)

### Driving test range (1975)

> Each vehicle temporarily has attached to it a radio transmitter having a very limited range and transmitting an identifying signal for the particular vehicle. Buried throughout the range are sensing loops which receive the transmissions from the vehicles and combine the vehicle identification with vehicle operation information being generated at the moment.

https://patents.google.com/patent/US3991485A/en

### High resolution timing recording system (1977)

> Radio frequency receiving means, such as loops buried in the track, are located at each of the stations and are adapted to receive an interval of signals from each transmitter on each entity when it passes within a reception area of each station.

https://patents.google.com/patent/US4142680A/en

Similars:
https://patents.google.com/patent/US4074117A - 1976, Grand Prix


### System for identifying and displaying data transmitted by way of unique identifying frequencies from multiple vehicles (1982)

Lay down two antennae in known proximity, connect them via splitter/combiner to a single coax, and use a single decoder to detect passing speeds as well.

> A system for detecting and indicating relative location and speed of a number of vehicles on a closed racetrack where each vehicle generates a unique radio frequency identification signal

FIG 13 - passing waveform

https://patents.google.com/patent/US4449114A/en

### Device for determining the moment when competitors in a race are passing the finishing line (1980)

Two perpendicular antennas receive, RSSI-based passing time detection.

https://patents.google.com/patent/US4315242A/en

Same:
https://patents.google.com/patent/US4274076A/en (1979)

### Timing apparatus (1989)

> timing apparatus for determining precisely when vehicles pass over a particular line

> FIG. 4 is a graph of signal strength versus time for a transmitter passing over a receiving loop of the antenna array;

It describes a probably quite reliable passing point detector: find the time when the signal strength hit a certain point the first time, then find when it left the same point the last time. Passing time is right in the middle.

https://patents.google.com/patent/US5091895A/en

### Automatic vehicular timing and scoring system (1991)

> As the transmitting antenna moves away from the first antenna wire and moves toward the second antenna wire, the received signal falls off to a minimum 75. The minimum occurs when the transmitting antenna is midway between the antenna wires.

Mentions speed detection:
> The duration of the received signal depends on the speed of the passing car. The faster the car, the shorter the duration.

https://patents.google.com/patent/US5194843A/en

### Radio frequency identification interrogator signal processing system for reading moving transponders (1998)

> combining the received in-phase (I) and quadrature-phase (Q) components of the signal in a manner that cancels out the amplitude nulls and phase reversals caused by movement of the RF/ID transponder

It feels like a strong prior art to some later, still active patents...

https://patents.google.com/patent/US6122329A/en

### Method and device for automatic timing in mass sporting events (2002)

> From DE 39 29 048 A1, an automatic timer is known, in particular, for mass sporting events, in which an individual performance test takes place. In this connection, at the start, a code word in a transmission frequency is transmitted from a transponder to a reading apparatus, which starts a timer in the reading apparatus and stops upon finish. Manipulation should be impossible, such that the transmission frequency as well as the code word is evaluated.

https://patents.google.com/patent/US20030235116A1/en
https://patents.google.com/patent/EP0619907B1

### Data display system and method for an object traversing a circuit (Hewlett Packard, 2002)
OMFG. These ones patented contactless lap counting in 2002. Our patent system is seriously broken...

Nevertheless, it describes an active transponder and a loop antenna. Ohh: the transponder can emit measured data.

https://patents.google.com/patent/US6870466B2/en

### System for determining a position of a moving transponder (MyLaps, 2003)
Similar arrangement as US3546696A (1969), with some additions:
* transponder emits in UHF range (digital communication)
* ground-loop emits a unique conde encoded in the magnetic field

> "said transponder, said transponder being adapted to determine a plurality of signal strengths of said received magnetic field signal"
> "processing means adapted to determine said position in accordance with a plurality of said received signal strengths determined by said moving transponder"

https://patents.google.com/patent/US6864829B2/en

### Method and device for automatic timing in mass sporting events (2003)

Transponder starts timing when passing the start loop, stops at finish loop.

https://patents.google.com/patent/US7057975B2/en

### System for determining the position of a transponder (2000)
While it deals with "position transverse to the course", it starts with something pure gold:

> Such systems are known from the state of the art. In these systems in general the object is to determine the position in the direction of movement whereby field strength measurements are used. An example thereof is described in U.S. Pat. No. 5,621,411.

https://patents.google.com/patent/US7006008B1/en

### Determining position and speed (2001)

Doppler-shift based speed detection

https://patents.google.com/patent/GB2376585A/en

### Detecting the passing between a transmitter and a detector (2011)

Passing time identified by phase transition in the emitted signal. Requires a transponder which align symbols to symbol rate.

https://patents.google.com/patent/US20120087421

### Determining the passing time of a moving transponder (2014, MyLaps)

Solves the problem of passing point detection if the transponder is not parallel or perpendicular to the moving direction. Use two perpendicular transmit antennae and a single receive antenna. Active until 2034, but is an obvious modification of US4315242A (1980). Note, the 1980 patent claims:

> signal received by one antenna is above a relatively high first threshold value and the signal received by the other antenna is below a relatively low second threshold value

https://patents.google.com/patent/EP3035298B9/en

### Electronic timing and recording apparatus (Kodak, 1986)

Ultrasonic detectors along the side of the track :D

https://patents.google.com/patent/US4752764A/en


### Radio frequency identification apparatuses (2007)

Near field, BPSK, CRC; the full package...

https://patents.google.com/patent/US7777610B2/en


## OTHERS

### Method and system for detecting an event on a sports track

detecting malfunctioning of time monitoring equipment used for time monitoring at active sports events performed on a sports track

"at least two track segments across a width of the sports track"

https://patents.google.com/patent/EP2646988B1


### Non-real time

https://patents.google.com/patent/EP0619907B1 

### Passive transmitter
https://patents.google.com/patent/EP0619907A1/en
https://patents.google.com/patent/US20060097847A1/en

### Bullshit

https://patents.google.com/patent/US6020851A/en - someone managed to patent LiveTime :D
https://patents.google.com/patent/US10481560B2/en - adding a motion sensor


### Noncategorized

https://patents.google.com/patent/US5436611A/en
https://patents.google.com/patent/EP2504662B1
https://patents.google.com/patent/US4551725A/en
https://patents.google.com/patent/US5511045A/en
https://patents.google.com/patent/DE3248565A1/
https://patents.google.com/patent/US5416486A/en (high frequency tag decreases size)
https://patents.google.com/patent/US3714649A/en
https://patents.google.com/patent/US8135614B2/ 
https://patents.google.com/patent/DE3248565A1/