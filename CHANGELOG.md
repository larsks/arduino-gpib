# Version 6.1 (March 3, 2014)

- bug fix especially at command interpreter level ;


- command syntax is now compatible with the `++` language;

- for compatibility reasons the controller is now much more silent when executing commands (but see the `++verbose` command below);

- reads from GPIB are not buffered anymore: char received from GPIb are sent to USB a soon as they are received. This enable this version of the controller to receive bulk binary data;

- for compatibility reasons the escape char has been changed from `\` to Ascii ESC (0x1B);

- `++addr` has a new default value of 2;

- `++eot_char` and `++eot_enable` commands have been implemented with default values of 10 (0xA) and 0 respectively;

- `++auto` is no more a toggle and is fully compatible now. The new syntax is `++auto [0|1]` and the default is 0;

- `++eoi` has been implemented with syntax `++eoi [0|1]` and a default value of 1;

- `++eos` has been implemented with syntax `++eos [0|1|2|3`] and a default value is 1;

- `++read` is completely unbuffered now. It terminates in front of an EOI signal detect OR an EOS char received from GPIB.  `++read eoi` changes this logic to EOI detection only. This is useful (and works) for binary transfers - e.g. screen dumps.

- `++ver` returns ` the string `ARDUINO GPIB firmware by E. Girlando Version 6.1 `.

- `++read_tmo_ms` has been implemented with syntax `++read_tmo-ms [100-9999]` and a default value of 200;

Two additional proprietary commands (not usually included in the `++` set) have been implemented:

- `++verbose` this command toggles the controller between  verbose and silent mode. When in verbose mode the controller assumes an human is working on the session: a `>` prompt is issued to the USB side of the session every time the controller is ready to accept a new command and most of the `++` commands now print a useful feedback answers (i.g. the new set value or error messages). This breaks the full `++` compatibility. Do not use verbose mode when trying to use third party software. Default is verbose OFF.

- `++dcl` sends an unaddressed DCL (Universal Device Clear) command to the GPIB. It is a message to the device rather than to its interface. So it is left to the device how to react on a DCL. Typically it generates a power on reset.
This version of the controller successfully reacts to the Prologic.exe program included in the KE5FX GPIB toolkit. I was not able to test the 7470.exe program as I do not have any instrument compatible with the toolkit (I indeed tryed with my TEK 2232 scope, but it failed...).
