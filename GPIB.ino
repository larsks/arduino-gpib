/*
Arduino USB to GPIB firmware by E. Girlando is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License.
Permissions beyond the scope of this license may be available at emanuele_girlando@yahoo.com.
*/

// #define DEBUG_BUILD
// #define DEEP_DEBUG_BUILD 


/*
 Implements some of the CONTROLLER functions; CIC only;
 Almost compatible with the "++" command language present in other de facto standard professional solutions.
 DEVICE functions: TODO (the listen part is needed to get bulk binary data from instruments.. -such as screen dumps).
 The talker function I cannot imagine what can be useful for.
 
 Principle of operation:
 Receives commands from the USB in (buffered) lines, 
 if "++" command call the handler,
 otherwise sends it to the GPIB bus.
 To receive from GIPB you must issue a ++read command or put the controller in auto mode with ++auto 1.
 The receiver is unbeffered (byte are senti to USB as soon as they are received).
 */

/*
 Together with the comments aside,these definitions define the mapping between the Arduino pins and the GPIB connector.
 It actually defines the hardware connections required for this sketch to work.
 NOTE:
 GPIB pins 10, 17-24 goto GND, but 
 GPIB pin 17 (REN) has to be investigated further..
 GPIB pin 10 (SRQ) has to be investigated further..
 GPIB pin 12 should be connected to the cable shield (not used here - leave it n/c)
 */
#define DIO1  A0  /* GPIB 1  : I/O data bit 1     - Arduino PORTC bit 0 */
#define DIO2  A1  /* GPIB 2  : I/O data bit 2     - Arduino PORTC bit 1 */
#define DIO3  A2  /* GPIB 3  : I/O data bit 3     - Arduino PORTC bit 2 */
#define DIO4  A3  /* GPIB 4  : I/O data bit 4     - Arduino PORTC bit 3 */
#define DIO5  A4  /* GPIB 13 : I/O data bit 5     - Arduino PORTC bit 4 */
#define DIO6  A5  /* GPIB 14 : I/O data bit 6     - Arduino PORTC bit 5 */
#define DIO7  4   /* GPIB 15 : I/O data bit 7     - Arduino PORTD bit 4 */
#define DIO8  5   /* GPIB 16 : I/O data bit 8     - Arduino PORTD bit 5 */
#define EOI   12  /* GPIB 5  : End Or Identify    - Arduino PORTB bit 4 */
#define DAV   11  /* GPIB 6  : Data Valid         - Arduino PORTB bit 3 */
#define NRFD  10  /* GPIB 7  : Not Ready For Data - Arduino PORTB bit 2 */
#define NDAC  9   /* GPIB 8  : Not Data Accepted  - Arduino PORTB bit 0 */
#define IFC   8   /* GPIB 9  : Interface Clear    - Arduino PORTB bit 1 */
#define ATN   7   /* GPIB 11 : Attention          - Arduino PORTD bit 7 */
/* 
 NOTE for the entire code: 
 Remember GPIB logic: HIGH means not active, deasserted; LOW means active, asserted; 
 also remember that ATmega's pins set as inputs are hi-Z (unless you enable the internal pullups).
 */
/*
GPIB BUS commands
*/
// Universal Multiline commands - all devices affected. No need to address a destination.
#define DCL 0x14
#define LLO 0x11
#define UNL 0x3F
#define UNT 0x5F
#define SPE 0x18
#define SPD 0x19
#define PPU 0x15
// Addressed commands
#define SDC 0x04
#define GTL 0x01
#define GET 0x08




#define SUCCESS false
#define FAIL true
#define IN   1
#define OUT  0
#define CONTROLLER  true
#define DEVICE      false
/* 
***** GLOBAL BUFFERS 
*/
#define BUFFSIZE 128              // too short? 
char com[BUFFSIZE] = "",          // USB input string buffer
     *comp = com,                 // pointer floating in com buffer
     *come = com + BUFFSIZE - 1;  // pointer to the far end of com buffer
// NOTE: use of ponters rather than indexes is a personal (BAD) choice: it can lead to potential portability issues, but that's it.


#define ESC  0x1B         // the USB escape char for "+"s, CRs and NLs.
#define CR   0xD
#define NL   0xA
#define PLUS 0x2B
char *EOSs = "\r\n";      // string containing a list of all possible GPIB terminator chars.
boolean itwascmd=false;   // flag to know if the last USB input line was a "++" command or not.

/*
  Controller state variables.
  They should be collected in a struct...and acconpained whth reoutines to save and load them to/from EEPROM
  
  Their values are initialized here and changes are managed by "++" command handlers
 */     
#define MYADDR 5          // my gpib adress - The Controller-In-Charge (me in this case) for a bus is almost always at PAD 0 */
int htimeout = 200;       // Handshake timeout. Also known as read_tmo_ms. Its actual value has to be found by
                          // trial&error depending on the actual application.
boolean cmode = CONTROLLER;     // controller mode (never changed at the moment.. only controller mode implemented)
byte addr = 2;            // GPIB address of actual communication counterpart (tipically a device or instrument).

byte eos=1;               // end of string flag to control how messages to GPIB are terminated.
boolean eoi=true;         // by default asserts EOI on last char written to GPIB
boolean eoi_only=false;   // GPIB input string termination: default is by (EOI || EOS) but "++read eoi" requires an eoi only behavior.
boolean eot_enable=false; // do we append a char to received string before sending it to USB?
char eot_char = 0xA;      // if eot_enable this is the char in use.
boolean automode = false; // read after write 
boolean verbose=false;    // if set the controller expect a human interaction but loose compatibility; silent otherwise but with more compatibility.
/*
  end of Controller state variables
 */
 
 
void setup() {

  Serial.begin(115200); 
  flush_serial();
  
  // initialize gpib lines
  pinMode(ATN, OUTPUT);  
  digitalWrite(ATN, HIGH); 
  pinMode(EOI, OUTPUT);  
  digitalWrite(EOI, HIGH);
  pinMode(DAV, OUTPUT);  
  digitalWrite(DAV, HIGH); 
  pinMode(NDAC, OUTPUT); 
  digitalWrite(NDAC, LOW);
  pinMode(NRFD, OUTPUT); 
  digitalWrite(NRFD, LOW);
  pinMode(IFC, OUTPUT); 
  digitalWrite(IFC, HIGH);
  (void) get_dab();  // just to set all data lines in a known not floating state..

  if (verbose) print_ver();

#ifdef DEBUG_BUILD
  Serial.println(F("Debug enable"));
#endif

}

// sometime things go wrong (e.g. buffer overflows, syntax erros in "++" cmds) and 
// we do need to discard everything left pending in the serial buffer..)
void flush_serial(void) {
  while( Serial.available() != 0 ) {
    
#ifdef DEBUG_BUILD
      Serial.print(F("FLUSHED 0x"));
      Serial.println(Serial.read(),HEX);
#else
      Serial.read();
#endif 
     }
}
  

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////
/////////// MAIN LOOP
///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////
void loop() {
  
  flush_serial();                    // ensure nothing is pending just before prompting the user..
  if (verbose) Serial.print("> ");   // humans like to be promted for input...
  
  getUSBline();          // gets the next USB input line and if it is a "++" command exec the handler.
  
  if (*com) {            // if somthing arrived .. and it should... 
#ifdef DEBUG_BUILD
    Serial.print("Sending ");
    Serial.print(com);
    Serial.println(" to GPIB BUS");
#endif      
    gpibTalk(com);
  }

  if (*com && automode && !itwascmd)  gpibReceive();  else if (verbose) Serial.println();
  
  *com = NULL; itwascmd=false;
} // end loop

void getUSBline() {
  static byte c = 0;
  static boolean isesc=false;    // escape flag
  static byte gotplus=0;         // '+' counter
  do { 

    if (Serial.available()>0) { 
      if (Serial.available() >=63) { // chars coming in too fast....
       if (verbose) Serial.println(F("Serial buffer overflow - line discarded. Try setting non buffered I/O in your USB data source")); 
       *com = NULL; comp = com;
       return;
      }
      
      c=Serial.read();

#ifdef DEEP_DEBUG_BUILD      
      Serial.print("0x"); 
      Serial.print(c, HEX);
      Serial.print(": "); 
      Serial.print((char)c);
      Serial.print(F(" - Serial buffer = ")); Serial.print(Serial.available()); 
      Serial.print(" - Pluses: "); 
      Serial.println(gotplus);
#endif
      
      switch ( c ) { 

        case ESC:
#ifdef DEBUG_BUILD
        Serial.println(F("ESC!!!"));
#endif
          if (isesc) goto loadchar;     // escaped escape (!)
          else isesc = true;
          break;
          
        case PLUS:
           if (isesc) goto loadchar;    // escaped '+'
           if (comp == com && gotplus <2) gotplus++; // only if comp has not yet been moved (++ at the beginning..)
           break;         
  
        case CR:
        case NL:
         // if (isesc) goto loadchar;  
          if (isesc) { Serial.println("CR or LF inserted");goto loadchar; }
          
          *comp = NULL; // replace USBeos with null
          if (gotplus == 2) {  // got a "++" at the beginnig: its a command!
            cmdparse();        // parse it
            itwascmd=true;
          } 
          comp = com; isesc=false; gotplus=0;
          return;
          break;
  
        default:
        loadchar:       // collect c in com, ensuring com does not overflow */
          if ( comp < come ) { *comp++ = c; } 
          else {     // buffer overflow; entire line discarded
            Serial.print(F("USB buffer overflow: limit input size to ")); Serial.print(BUFFSIZE-1); Serial.println(" chars."); 
            //flush_serial(); 
            *com = NULL; comp = com; isesc=false;
            return;
          }
          isesc=false;
       } //end switch
    } // end if serial.available()
  } while(1); 
Serial.print(F("ASSERT error: statement should be never reached in getUSBline()"));
}

/*
    command parser
 */
struct cmd_info { 
  char* opcode; 
  void (*handler)(void); 
};

static cmd_info cmds [] = { 
  { "addr",        addr_h   }, 
  { "ver" ,        print_ver    },
  { "verbose" ,    verbose_h    },
  { "mode" ,       mode_h       },
  { "read_tmo_ms", tmo_h        },
  { "auto",        automode_h   },
  { "read",        read_h       },
  { "clr",         clr_h        },
  { "trg",         trg_h        },
  { "llo",         llo_h        },
  { "loc",         loc_h        },
  { "eos",         eos_h        },
  { "eoi",         eoi_h        },
  { "eot_enable",  eot_enable_h },
  { "eot_char",    eot_char_h   },
  { "dcl",         dcl_h        },
  { "ifc",         ifc_h        }
}; 

/*
  Called when a "++" has been founf at the beginning of USB input line
*/
void cmdparse() { 
cmd_info *cmdp,
         *cmde = cmds + sizeof( cmds ) / sizeof( cmds[0] );
char *param; // pointer to comman parameter(s)
  
#ifdef DEBUG_BUILD
  Serial.print("Parse: "); 
  Serial.print(com); Serial.print(" - lengh:");Serial.println(strlen(com));
#endif

  if (*com == (NULL || CR || NL) ) return; // empty line: nothing to parse.
  
  param=strtok(com, " \t"); // additional calls to strtok possible in handlers routines
  for (cmdp = cmds; cmdp != cmde; cmdp++ ) {
#ifdef DEEP_DEBUG_BUILD
    Serial.print("Parse: "); 
    Serial.print(cmdp->opcode); Serial.print(" Vs ");Serial.println(param);
#endif  
    if( 0 == strcmp(cmdp->opcode, param) ) {    // found a valid command
      (*cmdp->handler)();                       // call the corresponding handler
      break;                                    // done.
    }
  }
  
  if (cmdp == cmde && verbose) {
    Serial.print(param); Serial.println(F(": unreconized command.")); 
  }

*com=NULL; comp=com; //Done.

}


/*
command handlers
 */
void addr_h() { 
  char *param, *pend; 
  int temp;

  if ( (param=strtok(NULL, " \t")) ) {  
    // Serial.print("param: "); Serial.println(param);
    temp = strtol(param, &pend, 10);
    if (*pend != NULL) { if (verbose) Serial.println(F("Syntax error.")); return;}
    if (temp<1 || temp > 30) { 
        if (verbose) Serial.println(F("address out of range: valid address range is [1-30]."));return;} 
    addr=temp; if (!verbose) return;
  }
  Serial.println(addr); 
}

void tmo_h() { 
  char *param, *pend; 
  int temp;

  if ( (param=strtok(NULL, " \t")) ) {  
    // Serial.print("param: "); Serial.println(param);
    temp = strtol(param, &pend, 10);
    if (*pend != NULL) { if (verbose) Serial.println(F("Syntax error.")); return;}
    if (temp<100 || temp > 9999) { 
        if (verbose) Serial.println(F("read_tmo_ms out of range: valid address range is [100-9999]."));return;}
    htimeout=temp; if (!verbose) return;
  }
  Serial.println(htimeout); 
}
void eos_h() { 
  char *param, *pend; 
  int temp;

  if ( (param=strtok(NULL, " \t")) ) {  
    // Serial.print("param: "); Serial.println(param);
    temp = strtol(param, &pend, 10);
    if (*pend != NULL) { if (verbose) Serial.println(F("Syntax error.")); return;}
    if (temp<0|| temp > 3) { 
        if (verbose) Serial.println(F("eos out of range: valid address range is [0-3]."));return;}
    eos=temp; if (!verbose) return;
  }
  Serial.println(eos); 
}
void eoi_h() { 
  char *param, *pend; 
  int temp;

  if ( (param=strtok(NULL, " \t")) ) {  
    temp = strtol(param, &pend, 10);
    if (*pend != NULL) { if (verbose) Serial.println(F("Syntax error.")); return;}
    if (temp<0|| temp > 1) { 
        if (verbose) Serial.println(F("eoi: valid address range is [0|1]."));return;}
    eoi = temp ? true : false;  if (!verbose) return;
  }
  Serial.println(eoi); 
}
void mode_h() { 
  char *param, *pend; 
  int temp;

  if ( (param=strtok(NULL, " \t")) ) {  
    temp = strtol(param, &pend, 10);
    if (*pend != NULL) { if (verbose) Serial.println(F("Syntax error.")); return;}
    if (temp<0|| temp > 1) { 
        if (verbose) Serial.println(F("mode: valid address range is [0|1]."));return;}
    // cmode = temp ? CONTROLLER : DEVICE;  if (!verbose) return;
       cmode=CONTROLLER;  // only controller mode implemented here... 
  }
  Serial.println(cmode); 
}
void eot_enable_h() { 
  char *param, *pend; 
  int temp;

  if ( (param=strtok(NULL, " \t")) ) {  
    temp = strtol(param, &pend, 10);
    if (*pend != NULL) { if (verbose) Serial.println(F("Syntax error.")); return;}
    if (temp<0|| temp > 1) { 
        if (verbose) Serial.println(F("eoi: valid address range is [0|1]."));return;}
    eot_enable = temp ? true : false;  if (!verbose) return;
  }
  Serial.println(eot_enable); 
}
void eot_char_h() { 
  char *param, *pend; 
  int temp;

  if ( (param=strtok(NULL, " \t")) ) {  
    temp = strtol(param, &pend, 10);
    if (*pend != NULL) { if (verbose) Serial.println(F("Syntax error.")); return;}
    if (temp<0|| temp > 256) { 
        if (verbose) Serial.println(F("eot_char: valid address range is [0-256]."));return;}
    eot_char = temp;  if (!verbose) return;
  }
  Serial.println((byte)eot_char); 
}

void automode_h() { 
char *param, *pend; 
int temp;

  if ( (param=strtok(NULL, " \t")) ) {  
    temp = strtol(param, &pend, 10);
    if (*pend != NULL) { if (verbose) Serial.println(F("Syntax error.")); return;}
    if (temp<0|| temp > 1) { 
        if (verbose) Serial.println(F("automode: valid address range is [0|1]."));return;}
    automode = temp ? true : false;  if (!verbose) return;
    !automode ? 0 : Serial.println(F("WARNING: automode ON can generate \"addressed to talk but nothing to say\" errors in the devices \nor issue read command too soon."));
  }
  Serial.println(automode); 
}

void print_ver() {
  Serial.println(F("ARDUINO GPIB firmware by E. Girlando Version 6.1")); 
}
void read_h() { 
char *param, *pend; 
int temp;

  if ( (param=strtok(NULL, " \t")) ) 
    if (0 == strncmp(param, "eoi", 3)) eoi_only=true;
  gpibReceive();
  eoi_only=false;
}


void clr_h() { 
  if ( sendGPIB_Acmd(SDC) )  { 
    Serial.println(F("clr_h: sendGPIB_Acmd failed")); 
    return; 
  }  
}
void llo_h() {
   if ( sendGPIB_Ucmd(LLO) )  { 
    Serial.println(F("llo_h: sendGPIB_Ucmd failed")); 
    return; 
  }  
}

/*
The (Universal) Device Clear (DCL) can be sent by any active controller and is recognized by all devices,
 however it is a message to the device rather than to its interface. 
 So it is left to the device how to react on a Universal Device Clear.
 There is no assumption (at least not in IEEE-488.1) what the device should do (it can even ignore the DCL).
 */
 void dcl_h() {
   if ( sendGPIB_Ucmd(DCL) )  { 
    Serial.println(F("dcl_h: sendGPIB_Ucmd failed")); 
    return; 
  }  
}
void loc_h() {
  if ( sendGPIB_Acmd(GTL) )  { 
    Serial.println(F("loc_h: sendGPIB_Acmd failed")); 
    return; 
  }  
}
 
/*
 The Interface Clear (IFC) is conducted by asserting the IFC line for at least 100 milliseconds, 
 which is reserved to the system controller. 
 The defined effect is that all interfaces connected to the bus return to their idle state 
 (putting the whole bus into a quiescent state), 
 and the controller role is returned to the system controller (if there is another controller active).
 */
void ifc_h() { 
  gpibIFC(); 
}

void trg_h() { 
if ( sendGPIB_Acmd(GET) )  { 
    Serial.println(F("trg_h: sendGPIB_Acmd failed")); 
    return; 
  }  

}

void verbose_h() { 
    verbose = !verbose;
    Serial.print("verbose: ");
    Serial.println(verbose ? "ON" : "OFF");
}
/*
    end of command handlers
 */

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////
/////////// GPIB LOW LEVEL ROUTINES
///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////
#ifdef DEBUG_BUILD
void gpibLineStatus(void) {
  pinMode(ATN, INPUT_PULLUP);   
  Serial.print(" ATN=");   
  Serial.print(digitalRead(ATN));
  pinMode(DAV, INPUT_PULLUP);   
  Serial.print(", DAV=");  
  Serial.print(digitalRead(DAV));
  pinMode(NRFD, INPUT_PULLUP);  
  Serial.print(", NRFD="); 
  Serial.print(digitalRead(NRFD));
  pinMode(NDAC, INPUT_PULLUP);  
  Serial.print(", NDAC="); 
  Serial.print(digitalRead(NDAC));
  pinMode(EOI, INPUT_PULLUP);   
  Serial.print(", EOI=");  
  Serial.print(digitalRead(EOI));
  Serial.print(", DIO8-1=");
  pinMode(DIO8, INPUT_PULLUP);  
  Serial.print(digitalRead(DIO8));
  pinMode(DIO7, INPUT_PULLUP);  
  Serial.print(digitalRead(DIO7));
  pinMode(DIO6, INPUT_PULLUP);  
  Serial.print(digitalRead(DIO6));
  pinMode(DIO5, INPUT_PULLUP);  
  Serial.print(digitalRead(DIO5));
  pinMode(DIO4, INPUT_PULLUP);  
  Serial.print(digitalRead(DIO4));
  pinMode(DIO3, INPUT_PULLUP);  
  Serial.print(digitalRead(DIO3));
  pinMode(DIO2, INPUT_PULLUP);  
  Serial.print(digitalRead(DIO2));
  pinMode(DIO1, INPUT_PULLUP);  
  Serial.println(digitalRead(DIO1));

  //reset pins at inital status
  pinMode(ATN, OUTPUT);  
  digitalWrite(ATN, HIGH); 
  pinMode(EOI, OUTPUT);  
  digitalWrite(EOI, HIGH);
  pinMode(DAV, OUTPUT);  
  digitalWrite(DAV, HIGH); 
  pinMode(NDAC, OUTPUT); 
  digitalWrite(NDAC, LOW);
  pinMode(NRFD, OUTPUT); 
  digitalWrite(NRFD, LOW);
}
#endif

/*
********** GPIB LOW LOW level routines
*/
/*
The following two functions read/write data from the GPIB.
In version 1 they where implemented by a sequence of pinmode(), pinwrite(9 and pinread().
Here in version 2 they are implemented by bitwise operations directly on the Arduino register PORTs.
The old code remain commented to give an idea of what the bitwise ops are supposed to do.

Theory of the following bitwise operations:
 We want to move some bit from v2 to v1, leaving the other bits in v1 unchanged (all variables unsigned!).
 M is the mask of the target bits in v1. It contains 1s in the positions where the bits to transfer are 
 in v1). e.g M=0b00110000 (..want to move bits 4 and 5).
 The statement: 1 = (v1 & ~M) | (v2 & M)
 will take bits 4 and 5 from v2 and inserts them in v1 leaving the other v1 bits unchanged.
 
 For meaning of DDRD, DDRC, PORTD and PORTD see the Arduino documentation.

 References to variable x in set_dab and the return value in get_dab are inverted because of the GPIB logic.
 
 Here the "DAB" mnemonic is used to indicate a data line transfer, either actual data bytes (DABs) or command bytes
 
 */
byte get_dab() {
/*
  pinMode(DIO1, INPUT_PULLUP); bitWrite(x, 0, !digitalRead(DIO1));
   pinMode(DIO2, INPUT_PULLUP); bitWrite(x, 1, !digitalRead(DIO2));
   pinMode(DIO3, INPUT_PULLUP); bitWrite(x, 2, !digitalRead(DIO3));
   pinMode(DIO4, INPUT_PULLUP); bitWrite(x, 3, !digitalRead(DIO4));
   pinMode(DIO5, INPUT_PULLUP); bitWrite(x, 4, !digitalRead(DIO5));
   pinMode(DIO6, INPUT_PULLUP); bitWrite(x, 5, !digitalRead(DIO6));
   pinMode(DIO7, INPUT_PULLUP); bitWrite(x, 6, !digitalRead(DIO7));
   pinMode(DIO8, INPUT_PULLUP); bitWrite(x, 7, !digitalRead(DIO8));
*/
  
  DDRD = DDRD & 0b11001111 ; 
  PORTD = PORTD | 0b00110000; // PORTD bits 5,4 input_pullup
  DDRC = DDRC & 0b11000000 ; 
  PORTC = PORTC | 0b00111111; // PORTC bits 5,4,3,2,1,0 input_pullup
  return ~((PIND<<2 & 0b11000000)+(PINC & 0b00111111));  
}
void set_dab(byte x) {
  /*
   pinMode(DIO1, OUTPUT); digitalWrite(DIO1, bitRead(~x, 0));
   pinMode(DIO2, OUTPUT); digitalWrite(DIO2, bitRead(~x, 1));
   pinMode(DIO3, OUTPUT); digitalWrite(DIO3, bitRead(~x, 2));
   pinMode(DIO4, OUTPUT); digitalWrite(DIO4, bitRead(~x, 3));
   pinMode(DIO5, OUTPUT); digitalWrite(DIO5, bitRead(~x, 4));
   pinMode(DIO6, OUTPUT); digitalWrite(DIO6, bitRead(~x, 5));
   pinMode(DIO7, OUTPUT); digitalWrite(DIO7, bitRead(~x, 6));
   pinMode(DIO8, OUTPUT); digitalWrite(DIO8, bitRead(~x, 7));
   */
  DDRD = DDRD | 0b00110000;
  DDRC = DDRC | 0b00111111;
  PORTD = ( (PORTD&0b11001111) | (~x>>2 & 0b00110000) ) ;
  PORTC = ( (PORTC&0b11000000) | (~x    & 0b00111111) ) ;
}


// wait for "pin" to go at "level" or "timeout".
// return values:  false on success, true on timeout.
boolean Wait_level_on_pin(byte level, byte pin, int timeout) {
  int s_time, c_time;

  pinMode(pin, INPUT_PULLUP);
  s_time = millis();
  c_time = s_time;
  while (level == digitalRead(pin)) { 
    if( (c_time - s_time)>timeout ) return true;
    else c_time=millis(); 
  }
  return false;        //success!
}

/*
Source role.
 Write a single char on gpib managing the gpib 3wires handshake protocol with timeouts.
 See Intel AP-166; Figure2.
 
 Return values:
 0 on success,
 1 on timeout. 
 */
boolean gpibWrite(byte data) {
  // prepare to for handshaking
  pinMode(DAV, OUTPUT);

#ifdef DEEP_DEBUG_BUILD
  Serial.print("sending data:0x");
  Serial.print(data, HEX);
  Serial.print("("); 
  Serial.print((char)data);
  Serial.print(") ");
#endif

  // prepare to for handshaking
  digitalWrite(DAV, HIGH);

  //wait until (LOW == NDAC)
  if (Wait_level_on_pin(HIGH,NDAC,htimeout))  { 
    if (verbose) Serial.println(F("gpibWrite: timeout waiting NDAC")); 
    return true; 
  }
#ifdef DEEP_DEBUG_BUILD
  Serial.print("W1");
#endif
  // output data to DIO
  set_dab(data);

  //wait until (HIGH == NRFD)
  if (Wait_level_on_pin(LOW,NRFD,htimeout))  { 
    if (verbose) Serial.println(F("gpibWrite: timeout waiting NRFD")); 
    return true; 
  }
#ifdef DEEP_DEBUG_BUILD
  Serial.print(" W2");
#endif

  // confirm data is valid 
  digitalWrite(DAV, LOW);
#ifdef DEEP_DEBUG_BUILD
  Serial.print(" W3");
#endif

  // wait until (HIGH == NDAC)
  if (Wait_level_on_pin(LOW,NDAC,htimeout))  { 
    if (verbose) Serial.println(F("gpibWrite: timeout waiting NDAC")); 
    digitalWrite(DAV, HIGH); 
    return true; 
  }
#ifdef DEEP_DEBUG_BUILD
  Serial.print(" W4");
#endif

  digitalWrite(DAV, HIGH);
  set_dab(0); 
  delayMicroseconds(10);
#ifdef DEEP_DEBUG_BUILD
  Serial.println(" W5 - Done");
#endif

  return false;
}

/*
Acceptor role.
 Read a single char on gpib managing the gpib 3wires handshake protocol with timeouts.
 See Intel AP-166; Figure2.
 Params:
 data is the address of the byte to put the read data in;
 EOS is the string containing all possible EOS characters (e.g. CR, LF, ...); An empty EOS means no use of end character..
 
 Return values:
 0 on success,
 1 on last char, 
 2 on timeout.
 */
byte gpibRead(byte *data) {

// prepare to for handshaking
pinMode(NRFD, OUTPUT); 
pinMode(NDAC, OUTPUT); 
pinMode(EOI, INPUT_PULLUP);
pinMode(DAV, INPUT_PULLUP);
delayMicroseconds(10);

// ensure NDAC is LOW as we left it last time we were called
digitalWrite(NDAC, LOW);

// Ready for more data  
digitalWrite(NRFD, HIGH);

// wait until (LOW == DAV) /Talker has finished setting data lines..
if (Wait_level_on_pin(HIGH,DAV,htimeout))  { 
  if (verbose) Serial.println(F("gpibRead: timeout waiting DAV")); 
  return 2; 
}
#ifdef DEEP_DEBUG_BUILD
  Serial.print("R1");
#endif

// read from DIO
*data = get_dab();

// NOT Ready for more data  
digitalWrite(NRFD, LOW);
#ifdef DEEP_DEBUG_BUILD
  Serial.print(" R2");
#endif

  /*
  There are three ways to terminate data messages: 
   - talker asserts the EOI (End or Identify) line, 
   - talker sends an EOS (End of String) character, and
   - byte count.
   When you use more than one termination method at a time, all methods are logically ORed together..
   Here we check for the first two only..(byte count not implemented yet).
   
   */
byte isEOI=0, isEOS=0;

if (LOW == digitalRead(EOI))    { 
  isEOI = 1; 
#ifdef DEEP_DEBUG_BUILD
  Serial.print(" -EOI-"); 
#endif 
} 
// eoi_only is a flag signaling that the high level ++read command has been issued with the "eoi" paramenter, 
// meaning that read must not check EOS and continue until EOI..
if (!eoi_only && strchr(EOSs, (char) *data)) { 
  isEOS = 1; 
#ifdef DEEP_DEBUG_BUILD
  Serial.print("-EOS-");  
#endif
};
//end of termination management


// data accepted
digitalWrite(NDAC, HIGH);

// wait until (HIGH == DAV) 
if (Wait_level_on_pin(LOW,DAV,htimeout))  { 
  if (verbose) Serial.println(F("gpibRead: timeout waiting DAV")); 
  return 2; 
}
#ifdef DEEP_DEBUG_BUILD
  Serial.print(" R3-0x");
  Serial.print(*data, HEX);
  Serial.print("("); 
  Serial.print(isprint((char)*data) ? (char)*data : '\0') ;
  Serial.print(") -> ");
#endif

digitalWrite(NDAC, LOW);

return byte (isEOI | isEOS); // ORed termination conditions
}
/*
    END OF LOW LEVEL ROUTINES
 */

//////////////////////////////////////////////////////////////////////////

/*
    FUNCTIONs (roles) ROUTINES
 */

/*
Talker role.
 */
void gpibTalk(char *outbuf) {
int i;

#ifdef DEBUG_BUILD
  Serial.print(F("gpibTalk: called to send command: ")); Serial.print(outbuf);
  Serial.print(" @ addr = "); Serial.println(addr);
#endif

  // prepare to talk
  pinMode(EOI, OUTPUT);
  // ensure EOI is not active
  digitalWrite(EOI, HIGH);

  if (set_comm_cntx(OUT))  { 
      if (verbose) Serial.println(F("gpibTalk: set_comm-cntx failed.")); 
      return; 
    } 

  // before sending the string to GPIB append the EOS to it
 
 #ifdef DEBUG_BUILD
      Serial.print("EOS=");Serial.println(eos);
#endif 
  switch (eos) {
  
    case 0:                       // appends CR+LF
      strcat(outbuf,EOSs);
      break;
    
    case 1:
    case 2:                      // appends CR or LF depending on eos value
      i=strlen(outbuf);
      outbuf[i]=EOSs[eos-1]; 
      outbuf[i+1]=NULL;
      break;
      
    case 3:                      // do not append anything
      break;
       
    default:                     // eos MUST be 0,1,2,or 3.
      if (verbose) Serial.println(F("gpibTalk: ASSERT error: Invalid eos.")); //never reached..
      return;
  };

  // ok, ready to start sending DABs out..
  // write outbuf but the last char 
  while (0 != *(outbuf + 1)) { // char by char write, up to the last but one..
    if (gpibWrite((byte)*outbuf))  { 
      if (verbose) Serial.println(F("gpibTalk: gpib write failed @4.")); 
      return; 
    } 
    delayMicroseconds(20);
    outbuf++;
  }

  // write last DAB (tipically the EOS..)
  if (eoi) digitalWrite(EOI, LOW);

  if (gpibWrite(*outbuf))  { 
    if (verbose) Serial.println(F("gpibTalk: gpib write failed @5.")); 
    return; 
  } 
  delayMicroseconds(20);
  
  digitalWrite(EOI, HIGH); // in  any case deassert EOI
    
  if ( sendGPIB_Ucmd(UNL) )  { 
    if (verbose) Serial.println(F("gpibTalk: sendGPIB_Ucmd failed.")); 
    return; 
  }  
}
/* 
 Listen role.
 Reads chars from GPIB an immediately send them to USB.
 Reads until char is signaled as last char, or a timeout occurred.
 */
boolean gpibReceive() {

#ifdef DEBUG_BUILD
  Serial.print(F("gpibReceive: called.")); 
  Serial.print(" From addr: "); Serial.println(addr);
#endif

  if (set_comm_cntx(IN))  { 
      if (verbose) Serial.println(F("gpibReceive: set_comm-cntx failed.")); 
      return true; 
    } 

  // receive data (DABs) bytes
  byte c; 
  boolean isLast;

  for (;;) {
    switch (gpibRead(&c)) 
    {
    case 0:  // everythink ok
#ifdef DEEP_DEBUG_BUILD
      Serial.println((char)c);
#else
      Serial.print((char)c);
#endif
      continue;

    case 1:  // everythink ok, but c has been signaled as the last char in the flow
      Serial.print((char)c); if (eot_enable) Serial.print((char)eot_char);
      break;

    case 2:  // read timeout: usually device turned off or wrong addr
      if (verbose) Serial.println(F("gpibReceive: Read timedout.")); 
      return true;
      break;

    default:
      if (verbose) Serial.println(F("gpibReceive: ASSERT error: gpibRead returned unexpected value.")); 
      return true; // never reached
      break;
    } 
    break; // breaks the for
  }   
  
  if ( sendGPIB_Ucmd(UNT) )  { // UNTalk the device
    if (verbose) Serial.println(F("gpibReceive: sendGPIB_Ucmd failed.")); 
    return true; 
    
  }  
  return false;
}

/*
       ************ GPIB COMMANDS SECTION
*/

// Uniline commands

// toggle the IFC line
void gpibIFC(void) {
  
  digitalWrite(IFC,LOW); 
  delayMicroseconds(128);
  digitalWrite(IFC, HIGH);
  delayMicroseconds(20);
}


/*
sends a Universal Multiline command to the GPIB BUS
P.O.O.: assert ATN, gpibwrites the command, deassert ATN 
*/
boolean sendGPIB_Ucmd(byte cmd) {
  // activate "attention" (puts the BUS in command mode)
  pinMode(ATN, OUTPUT); 
  digitalWrite(ATN, LOW); 
  delayMicroseconds(10);

  // issues the command 
  if (gpibWrite(cmd))  { 
    if (verbose) Serial.println(F("sendGPIB_Ucmd: gpib cmd write failed")); 
    return FAIL; 
  } 
  delayMicroseconds(10);

  // deactivate "attention" (returns the BUS in data mode)
  digitalWrite(ATN, HIGH); 
  delayMicroseconds(20);
  
  return SUCCESS;
}


/*
sends a Addressed command to the GPIB BUS
P.O.O.: assert ATN, address destination to listen, gpibwrites the command, unaddress all, deassert ATN 
*/
boolean sendGPIB_Acmd(byte cmd) {
  // activate "attention"
  pinMode(ATN, OUTPUT); 
  digitalWrite(ATN, LOW); 
  delayMicroseconds(10);

  // send ADDRESS for device clear
  if (gpibWrite((byte)(0x20 + addr)))  { 
    if (verbose) Serial.println(F("gpibSDC: gpib ADRRESS write failed")); 
    return FAIL; 
  } 
  delayMicroseconds(10);

  if (gpibWrite(cmd))  { 
    if (verbose) Serial.println(F("gpibSDC: gpib SDC (0x04) write failed")); 
    return FAIL; 
  } 
  delayMicroseconds(10);
  
  if (gpibWrite(UNL))  { 
    if (verbose) Serial.println(F("gpibSDC: gpib unlisten (UNL) write failed")); 
    return FAIL; 
  } 
  delayMicroseconds(10);

  // deactivate "attention"
  digitalWrite(ATN, HIGH); 
  delayMicroseconds(20);
  
  return SUCCESS;
}

///
/// set Command context
/// command context is the environment of the requested communication: who talks, who speaks.
/// direction parameter:
/// IN  = from the device to me (the controller); leaves the device in talk mode
/// OUT = from me (the controller) to the device; leaves the device in listen mode
///
boolean set_comm_cntx(byte direction) {
#ifdef DEEP_DEBUG_BUILD
  Serial.print(F("set_comm-cntx: direction:")); Serial.print(direction ? "IN" : "OUT");
  Serial.print(" remote addr="); Serial.println(addr);
#endif

  //attention
  /* If the ATN line is asserted, any messages sent on the data lines are heard by all devices, 
     and they are understood to be addresses or command messages.
   */
  digitalWrite(ATN, LOW); 
  delayMicroseconds(30);

/* Bits 0 through 4 (5bits) indicate the primary address of the device, for which the Talker/Listener assignment is intended.
   If bit 5 is high, the device should listen.
   If bit 6 is high, the device should talk.
   Bit 7 is a "don't care" bit. 
   addr = 1F (all ones) is reserved; it actually implements two commands:
   when sent to listen (0x3F) implements UNL (unlisten)
   when sent to talk (0x5F) implements UNT (untalk)
*/  
  // issue talker address 
  if (gpibWrite((byte)0x40 + (direction ?  addr : MYADDR) ))  { 
    if (verbose) Serial.println(F("set_comm_cntx: gpib write failed @1")); 
    return FAIL; 
  }
  delayMicroseconds(20);

  // listener address 
  if (gpibWrite((byte)(0x20 + (direction ? MYADDR : addr) )))  { 
    if (verbose) Serial.println(F("set_comm_cntx: gpib write failed @2")); 
    return FAIL; 
  }
  delayMicroseconds(20);

  // end of attention
  digitalWrite(ATN, HIGH); 
  delayMicroseconds(20);

  return SUCCESS;
}
