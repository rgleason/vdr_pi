vdr_pi
======

Voyage Data Recorder plugin for OpenCPN

Interval timer varies time between NMEA messages. When put to far left (minimum) the Record Positioner becomes active. The Record
Positioner moves the record pointer to ten records before the designated place. These are then sent through the NMEA channel.
When the Record Positioner is put to the far left the Interval Timer becomes active again and regular record processing occurs
starting at the current position.
When the Record Positioner is put to a lower position the file is re-read from the start and moves to the current position minus
ten as before.
