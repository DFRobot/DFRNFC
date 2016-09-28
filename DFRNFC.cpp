/**************************************************************************/
/*! 
    @file     DFRNFC.cpp
    @author   Adafruit Industries & DFRobot
	@license  BSD 
/**************************************************************************/
#include "DFRNFC.h"

const uint8_t wakeDummy[]={ PN532_WAKEUP,PN532_WAKEUP, 0x00, 0x00};

byte pn532ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
byte pn532response_firmwarevers[] = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5, 0x03};
uint8_t DataBlockAddr[] = {1,2,4,5,6,8,9,10,12,13,14,16,17,18,20,21,22,24,25,26,28,29,30,32,33,34,36,37,38,40,41,42,44,45,46,48,49,50,52,53,54,56,57,58,60,61,62};
bool isDataBlock[] ={0,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0};
uint8_t keyuniversal[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
// Uncomment these lines to enable debug output for PN532(SPI) and/or MIFARE related code
// #define PN532DEBUG
// #define MIFAREDEBUG

#define PN532_PACKBUFFSIZ 64
byte pn532_packetbuffer[PN532_PACKBUFFSIZ];

#ifndef _BV
    #define _BV(bit) (1<<(bit))
#endif


/**************************************************************************/
/*! 
    @brief  Setups the HW
*/
/**************************************************************************/
void DFRNFC::begin(Stream &theSerial) 
{
  _serial=&theSerial;
  _serial->write(wakeDummy,3); //you have to write a wakedummy before the command to wake up PN532
  
  SAMConfig(); // active PN532 to normal mode
  fS50found = 0;
}
 
 
/**************************************************************************/
/*! 
    @brief  Checks the firmware version of the PN5xx chip

    @returns  The chip's firmware version and ID
*/
/**************************************************************************/
uint8_t DFRNFC::getFirmwareVersion(uint8_t *version) {

  pn532_packetbuffer[0] = PN532_COMMAND_GETFIRMWAREVERSION;
  
  if (!sendCommandCheckAck(pn532_packetbuffer, 1))
    return 0;
  
  // read data packet
  readdata(pn532_packetbuffer, 13);
  
  // check some basic stuff
  if (0 != strncmp((char *)pn532_packetbuffer, (char *)pn532response_firmwarevers, 7)) {
    return 0;
  }
  
  version[0] = pn532_packetbuffer[7];  // IC hex 
  version[1] = pn532_packetbuffer[8];  // Version
  version[2] = pn532_packetbuffer[9];  // Revision
  version[3] = pn532_packetbuffer[10]; // Support

  return 1;
}


/**************************************************************************/
/*! 
    @brief  Sends a command and waits a specified period for the ACK

    @param  cmd       Pointer to the command buffer
    @param  cmdlen    The size of the command in bytes 
    @param  timeout   timeout before giving up
    
    @returns  1 if everything is OK, 0 if timeout occured before an
              ACK was recieved
*/
/**************************************************************************/
// default timeout of one second
boolean DFRNFC::sendCommandCheckAck(uint8_t *cmd, uint8_t cmdlen) 
{
  while(Serial.read() >= 0); //clear all the receive buff
  // write the command
  writecommand(cmd, cmdlen);
  boolean success = readack();
  return success;
}

/**************************************************************************/
/*! 
    @brief  Configures the SAM (Secure Access Module)
*/
/**************************************************************************/
boolean DFRNFC::SAMConfig(void) {
  pn532_packetbuffer[0] = PN532_COMMAND_SAMCONFIGURATION;
  pn532_packetbuffer[1] = 0x01; // normal mode;
  pn532_packetbuffer[2] = 0x14; // timeout 50ms * 20 = 1 second
  pn532_packetbuffer[3] = 0x01; // use IRQ pin!
  
  if (!sendCommandCheckAck(pn532_packetbuffer, 4))
       return false;

  // read data packet
  readdata(pn532_packetbuffer, 9);
  
  return  (pn532_packetbuffer[6] == 0x15);
}

/**************************************************************************/
/*! 
    Sets the MxRtyPassiveActivation byte of the RFConfiguration register
    
    @param  maxRetries    0xFF to wait forever, 0x00..0xFE to timeout
                          after mxRetries
    
    @returns 1 if everything executed properly, 0 for an error
*/
/**************************************************************************/
boolean DFRNFC::setPassiveActivationRetries(uint8_t maxRetries) {
  pn532_packetbuffer[0] = PN532_COMMAND_RFCONFIGURATION;
  pn532_packetbuffer[1] = 5;    // Config item 5 (MaxRetries)
  pn532_packetbuffer[2] = 0xFF; // MxRtyATR (default = 0xFF)
  pn532_packetbuffer[3] = 0x01; // MxRtyPSL (default = 0x01)
  pn532_packetbuffer[4] = maxRetries;

#ifdef MIFAREDEBUG
  _serial->print("Setting MxRtyPassiveActivation to "); _serial->print(maxRetries, DEC); _serial->println(" ");
#endif
  
  if (! sendCommandCheckAck(pn532_packetbuffer, 5))
    return 0x0;  // no ACK
  
  return 1;
}

/***** ISO14443A Commands ******/

/**************************************************************************/
/*! 
    Waits for an ISO14443A target to enter the field
    
    @param  cardBaudRate  Baud rate of the card
    @param  uid           Pointer to the array that will be populated
                          with the card's UID (up to 7 bytes)
    @param  uidLength     Pointer to the variable that will hold the
                          length of the card's UID.
    
    @returns 1 if everything executed properly, 0 for an error
*/
/**************************************************************************/
boolean DFRNFC::readPassiveTargetID(uint8_t cardbaudrate, uint8_t * uid, uint8_t * uidLength) {
  pn532_packetbuffer[0] = PN532_COMMAND_INLISTPASSIVETARGET;
  pn532_packetbuffer[1] = 1;  // max 1 cards at once (we can set this to 2 later)
  pn532_packetbuffer[2] = cardbaudrate;
  
  if (!sendCommandCheckAck(pn532_packetbuffer, 3))
    return 0x0;  // no cards read
  
  // read data packet
  readdata(pn532_packetbuffer, 19);
  // check some basic stuff

  /* ISO14443A card response should be in the following format:
  
    byte            Description
    -------------   ------------------------------------------
    b0..6           Frame header and preamble
    b7              Tags Found
    b8              Tag Number (only one used in this example)
    b9..10          SENS_RES
    b11             SEL_RES
    b12             NFCID Length
    b13..NFCIDLen   NFCID                                      */
  
#ifdef MIFAREDEBUG
    _serial->print("Found "); _serial->print(pn532_packetbuffer[7], DEC); _serial->println(" tags");
#endif
  if (pn532_packetbuffer[7] != 1) 
    return 0;
    
  uint16_t sens_res = pn532_packetbuffer[9];
  sens_res <<= 8;
  sens_res |= pn532_packetbuffer[10];
#ifdef MIFAREDEBUG
    _serial->print("ATQA: 0x");  _serial->println(sens_res, HEX); 
    _serial->print("SAK: 0x");  _serial->println(pn532_packetbuffer[11], HEX); 
#endif
  
  /* Card appears to be Mifare Classic */
  this->uidLength = *uidLength = pn532_packetbuffer[12];
#ifdef MIFAREDEBUG
    _serial->print("UID:"); 
#endif
  for (uint8_t i=0; i < pn532_packetbuffer[12]; i++) 
  {
    uid[i] = pn532_packetbuffer[13+i];
    _uid[i] = pn532_packetbuffer[13+i];
#ifdef MIFAREDEBUG
      _serial->print(" 0x");_serial->print(uid[i], HEX); 
#endif
  }
#ifdef MIFAREDEBUG
    _serial->println();
#endif
    
  fS50found = 1;
  return 1;
}


/***** Mifare Classic Functions ******/

/**************************************************************************/
/*! 
      Indicates whether the specified block number is the first block
      in the sector (block 0 relative to the current sector)
*/
/**************************************************************************/
boolean DFRNFC::mifareclassic_IsFirstBlock (uint32_t uiBlock)
{
  // Test if we are in the small or big sectors
  if (uiBlock < 128)
    return ((uiBlock) % 4 == 0);
  else
    return ((uiBlock) % 16 == 0);
}

/**************************************************************************/
/*! 
      Indicates whether the specified block number is the sector trailer
*/
/**************************************************************************/
boolean DFRNFC::mifareclassic_IsTrailerBlock (uint32_t uiBlock)
{
  // Test if we are in the small or big sectors
  if (uiBlock < 128)
    return ((uiBlock + 1) % 4 == 0);
  else
    return ((uiBlock + 1) % 16 == 0);
}

/**************************************************************************/
/*! 
    Tries to authenticate a block of memory on a MIFARE card using the
    INDATAEXCHANGE command.  See section 7.3.8 of the PN532 User Manual
    for more information on sending MIFARE and other commands.

    @param  uid           Pointer to a byte array containing the card UID
    @param  uidLen        The length (in bytes) of the card's UID (Should
                          be 4 for MIFARE Classic)
    @param  blockNumber   The block number to authenticate.  (0..63 for
                          1KB cards, and 0..255 for 4KB cards).
    @param  keyNumber     Which key type to use during authentication
                          (0 = MIFARE_CMD_AUTH_A, 1 = MIFARE_CMD_AUTH_B)
    @param  keyData       Pointer to a byte array containing the 6 byte
                          key value
    
    @returns 1 if everything executed properly, 0 for an error
*/
/**************************************************************************/
uint8_t DFRNFC::mifareclassic_AuthenticateBlock (uint8_t * uid, uint8_t uidLen, uint32_t blockNumber, uint8_t keyNumber, uint8_t * keyData)
{
  uint8_t len;
  uint8_t i;
  
  // Hang on to the key and uid data
  memcpy (_key, keyData, 6); 
  memcpy (_uid, uid, uidLen); 
  uidLength = uidLen;  

  #ifdef MIFAREDEBUG
  _serial->print("Trying to authenticate card ");
  DFRNFC::PrintHex(uid, uidLen);
  _serial->print("Using authentication KEY ");_serial->print(keyNumber ? 'B' : 'A');_serial->print(": ");
  DFRNFC::PrintHex(_key, 6);
  #endif
  
  // Prepare the authentication command //
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;   /* Data Exchange Header */
  pn532_packetbuffer[1] = 1;                              /* Max card numbers */
  pn532_packetbuffer[2] = (keyNumber) ? MIFARE_CMD_AUTH_B : MIFARE_CMD_AUTH_A;
  pn532_packetbuffer[3] = blockNumber;                    /* Block Number (1K = 0..63, 4K = 0..255 */
  memcpy (pn532_packetbuffer+4, _key, 6);
  for (i = 0; i < uidLength; i++)
  {
    pn532_packetbuffer[10+i] = _uid[i];                /* 4 byte card ID */
  }

  if (! sendCommandCheckAck(pn532_packetbuffer, 10+uidLength))
    return 0;

  // Read the response packet
  readdata(pn532_packetbuffer, 10);
  // check if the response is valid and we are authenticated???
  // for an auth success it should be bytes 5-7: 0xD5 0x41 0x00
  // Mifare auth error is technically byte 7: 0x14 but anything other and 0x00 is not good
  if (pn532_packetbuffer[7] != 0x00)
  {
    #ifdef PN532DEBUG
    _serial->print("Authentification failed: ");
    DFRNFC::PrintHexChar(pn532_packetbuffer, 10);
    #endif
    return 0;
  }

  return 1;
}

/**************************************************************************/
/*! 
    Tries to read an entire 16-byte data block at the specified block
    address.

    @param  blockNumber   The block number to authenticate.  (0..63 for
                          1KB cards, and 0..255 for 4KB cards).
    @param  data          Pointer to the byte array that will hold the
                          retrieved data (if any)
    
    @returns 1 if everything executed properly, 0 for an error
*/
/**************************************************************************/
uint8_t DFRNFC::mifareclassic_ReadDataBlock (uint8_t blockNumber, uint8_t * data)
{
  #ifdef MIFAREDEBUG
  _serial->print("Trying to read 16 bytes from block ");_serial->println(blockNumber);
  #endif
  
  /* Prepare the command */
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
  pn532_packetbuffer[1] = 1;                      /* Card number */
  pn532_packetbuffer[2] = MIFARE_CMD_READ;        /* Mifare Read command = 0x30 */
  pn532_packetbuffer[3] = blockNumber;            /* Block Number (0..63 for 1K, 0..255 for 4K) */

  /* Send the command */
  if (! sendCommandCheckAck(pn532_packetbuffer, 4))
  {
    #ifdef MIFAREDEBUG
    _serial->println("Failed to receive ACK for read command");
    #endif
    return 0;
  }

  /* Read the response packet */
  readdata(pn532_packetbuffer, 26);

  /* If byte 8 isn't 0x00 we probably have an error */
  if (pn532_packetbuffer[7] != 0x00)
  {
    #ifdef MIFAREDEBUG
        _serial->println("Unexpected response");
        DFRNFC::PrintHexChar(pn532_packetbuffer, 26);
    #endif
    return 0;
  }
    
  /* Copy the 16 data bytes to the output buffer        */
  /* Block content starts at byte 9 of a valid response */
  memcpy (data, pn532_packetbuffer+8, 16);

  /* Display data for debug if requested */
  #ifdef MIFAREDEBUG
    _serial->print("Block ");
    _serial->println(blockNumber);
    DFRNFC::PrintHexChar(data, 16);
  #endif

  return 1;  
}

/**************************************************************************/
/*! 
    Tries to write an entire 16-byte data block at the specified block
    address.

    @param  blockNumber   The block number to authenticate.  (0..63 for
                          1KB cards, and 0..255 for 4KB cards).
    @param  data          The byte array that contains the data to write.
    
    @returns 1 if everything executed properly, 0 for an error
*/
/**************************************************************************/
uint8_t DFRNFC::mifareclassic_WriteDataBlock (uint8_t blockNumber, uint8_t * data)
{
  #ifdef MIFAREDEBUG
  _serial->print("Trying to write 16 bytes to block ");_serial->println(blockNumber);
  #endif
  
  /* Prepare the first command */
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
  pn532_packetbuffer[1] = 1;                      /* Card number */
  pn532_packetbuffer[2] = MIFARE_CMD_WRITE;       /* Mifare Write command = 0xA0 */
  pn532_packetbuffer[3] = blockNumber;            /* Block Number (0..63 for 1K, 0..255 for 4K) */
  memcpy (pn532_packetbuffer+4, data, 16);          /* Data Payload */

  /* Send the command */
  if (! sendCommandCheckAck(pn532_packetbuffer, 20))
  {
    #ifdef MIFAREDEBUG
    _serial->println("Failed to receive ACK for write command");
    #endif
    return 0;
  }  
    delay(2);
  /* Read the response packet */
  readdata(pn532_packetbuffer, 10);


  return 1;  
}

/**************************************************************************/
/*! 
    Formats a Mifare Classic card to store NDEF Records 
    
    @returns 1 if everything executed properly, 0 for an error
*/
/**************************************************************************/
uint8_t DFRNFC::mifareclassic_FormatNDEF (void)
{
  uint8_t sectorbuffer1[16] = {0x14, 0x01, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1};
  uint8_t sectorbuffer2[16] = {0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1};
  uint8_t sectorbuffer3[16] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0x78, 0x77, 0x88, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  // Write block 1 and 2 to the card
  if (!(mifareclassic_WriteDataBlock (1, sectorbuffer1)))
    return 0;
  if (!(mifareclassic_WriteDataBlock (2, sectorbuffer2)))
    return 0;
  // Write key A and access rights card
  if (!(mifareclassic_WriteDataBlock (3, sectorbuffer3)))
    return 0;

  // Seems that everything was OK (?!)
  return 1;
}

/**************************************************************************/
/*! 
    Writes an NDEF URI Record to the specified sector (1..15)
    
    Note that this function assumes that the Mifare Classic card is
    already formatted to work as an "NFC Forum Tag" and uses a MAD1
    file system.  You can use the NXP TagWriter app on Android to
    properly format cards for this.

    @param  sectorNumber  The sector that the URI record should be written
                          to (can be 1..15 for a 1K card)
    @param  uriIdentifier The uri identifier code (0 = none, 0x01 = 
                          "http://www.", etc.)
    @param  url           The uri text to write (max 38 characters).
    
    @returns 1 if everything executed properly, 0 for an error
*/
/**************************************************************************/
uint8_t DFRNFC::mifareclassic_WriteNDEFURI (uint8_t sectorNumber, uint8_t uriIdentifier, const char * url)
{
  // Figure out how long the string is
  uint8_t len = strlen(url);
  
  // Make sure we're within a 1K limit for the sector number
  if ((sectorNumber < 1) || (sectorNumber > 15))
    return 0;
  
  // Make sure the URI payload is between 1 and 38 chars
  if ((len < 1) || (len > 38))
    return 0;
    
  // Setup the sector buffer (w/pre-formatted TLV wrapper and NDEF message)
  uint8_t sectorbuffer1[16] = {0x00, 0x00, 0x03, len+5, 0xD1, 0x01, len+1, 0x55, uriIdentifier, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t sectorbuffer2[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t sectorbuffer3[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t sectorbuffer4[16] = {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7, 0x7F, 0x07, 0x88, 0x40, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (len <= 6)
  {
    // Unlikely we'll get a url this short, but why not ...
    memcpy (sectorbuffer1+9, url, len);
    sectorbuffer1[len+9] = 0xFE;
  }
  else if (len == 7)
  {
    // 0xFE needs to be wrapped around to next block
    memcpy (sectorbuffer1+9, url, len);
    sectorbuffer2[0] = 0xFE;
  }
  else if ((len > 7) || (len <= 22))
  {
    // Url fits in two blocks
    memcpy (sectorbuffer1+9, url, 7);
    memcpy (sectorbuffer2, url+7, len-7);
    sectorbuffer2[len-7] = 0xFE;
  }
  else if (len == 23)
  {
    // 0xFE needs to be wrapped around to final block
    memcpy (sectorbuffer1+9, url, 7);
    memcpy (sectorbuffer2, url+7, len-7);
    sectorbuffer3[0] = 0xFE;
  }
  else
  {
    // Url fits in three blocks
    memcpy (sectorbuffer1+9, url, 7);
    memcpy (sectorbuffer2, url+7, 16);
    memcpy (sectorbuffer3, url+23, len-24);
    sectorbuffer3[len-22] = 0xFE;
  }
  
  // Now write all three blocks back to the card
  if (!(mifareclassic_WriteDataBlock (sectorNumber*4, sectorbuffer1)))
    return 0;
  if (!(mifareclassic_WriteDataBlock ((sectorNumber*4)+1, sectorbuffer2)))
    return 0;
  if (!(mifareclassic_WriteDataBlock ((sectorNumber*4)+2, sectorbuffer3)))
    return 0;
  if (!(mifareclassic_WriteDataBlock ((sectorNumber*4)+3, sectorbuffer4)))
    return 0;

  // Seems that everything was OK (?!)
  return 1;
}

/***** Mifare Ultralight Functions ******/

/**************************************************************************/
/*! 
    Tries to read an entire 4-byte page at the specified address.

    @param  page        The page number (0..63 in most cases)
    @param  buffer      Pointer to the byte array that will hold the
                        retrieved data (if any)
*/
/**************************************************************************/
uint8_t DFRNFC::mifareultralight_ReadPage (uint8_t page, uint8_t * buffer)
{
  if (page >= 64)
  {
    #ifdef MIFAREDEBUG
    _serial->println("Page value out of range");
    #endif
    return 0;
  }

  #ifdef MIFAREDEBUG
    _serial->print("Reading page ");_serial->println(page);
  #endif

  /* Prepare the command */
  pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
  pn532_packetbuffer[1] = 1;                   /* Card number */
  pn532_packetbuffer[2] = MIFARE_CMD_READ;     /* Mifare Read command = 0x30 */
  pn532_packetbuffer[3] = page;                /* Page Number (0..63 in most cases) */

  /* Send the command */
  if (! sendCommandCheckAck(pn532_packetbuffer, 4))
  {
    #ifdef MIFAREDEBUG
    _serial->println("Failed to receive ACK for write command");
    #endif
    return 0;
  }
  
  /* Read the response packet */
  readdata(pn532_packetbuffer, 26);
  #ifdef MIFAREDEBUG
    _serial->println("Received: ");
    DFRNFC::PrintHexChar(pn532_packetbuffer, 26);
  #endif

  /* If byte 8 isn't 0x00 we probably have an error */
  if (pn532_packetbuffer[7] == 0x00)
  {
    /* Copy the 4 data bytes to the output buffer         */
    /* Block content starts at byte 9 of a valid response */
    /* Note that the command actually reads 16 byte or 4  */
    /* pages at a time ... we simply discard the last 12  */
    /* bytes                                              */
    memcpy (buffer, pn532_packetbuffer+8, 4);
  }
  else
  {
    #ifdef MIFAREDEBUG
      _serial->println("Unexpected response reading block: ");
      DFRNFC::PrintHexChar(pn532_packetbuffer, 26);
    #endif
    return 0;
  }

  /* Display data for debug if requested */
  #ifdef MIFAREDEBUG
    _serial->print("Page ");_serial->print(page);_serial->println(":");
    DFRNFC::PrintHexChar(buffer, 4);
  #endif

  // Return OK signal
  return 1;
}





/**************************************************************************/
/*! 
    @brief  Tries to read the  ACK signal
*/
/**************************************************************************/
boolean DFRNFC::readack() {
  uint8_t ackbuff[6];
  readdata(ackbuff, 6);
  return (0 == strncmp((char *)ackbuff, (char *)pn532ack, 6));
}


int8_t DFRNFC::readdata(uint8_t* buff, uint8_t len) 
{
    _serial->readBytes(buff,len);
    return 0;
}

/**************************************************************************/
/*! 
    @brief  Writes a command to the PN532, automatically inserting the
            preamble and required frame details (checksum, len, etc.)

    @param  cmd       Pointer to the command buffer
    @param  cmdlen    Command length in bytes 
*/
/**************************************************************************/
void DFRNFC::writecommand(uint8_t* cmd, uint8_t cmdlen)
{
    uint8_t checksum;

    cmdlen++;

#ifdef PN532DEBUG
    _serial->print("\nSending: ");
#endif

    checksum = 0;
    _serial->write((uint8_t)PN532_PREAMBLE);
    _serial->write((uint8_t)PN532_STARTCODE1);
    _serial->write((uint8_t)PN532_STARTCODE2);

    _serial->write(cmdlen);
    _serial->write(~cmdlen + 1);

    _serial->write(PN532_HOSTTOPN532);
    checksum += PN532_HOSTTOPN532;


    for (uint8_t i=0; i<cmdlen-1; i++) 
    {
        _serial->write(cmd[i]);
        checksum += cmd[i];
    }
    
    _serial->write(~checksum + 1);
    _serial->write((uint8_t)PN532_POSTAMBLE);


} 

/**************************************************************************/
/*! 
    @brief  read bytes from data block, the address should be with the rage
            of 0 - 752, from Bytes 0 of block 1 to Byte 16 of block 62 

    @param  byteAddr    the address of the data to read. 16*47 data bytes  
    
    @returns   -1   if address is without the range
               -2   if failed to find a Mifare Classic card card
               -3   if authentication failed
               -4   if failed to read block
               data if succeed
*/
/**************************************************************************/
int DFRNFC::read(unsigned int byteAddr)
{   
    if(byteAddr > 751)
       return -1;   // without range
    if(!fS50found)  // if no s50 card has been find
    {
       if(!readPassiveTargetID(PN532_MIFARE_ISO14443A, _uid, &uidLength)) //try to find one
           return -2;
    }
    uint8_t numBlock = DataBlockAddr[byteAddr/16];
    uint8_t numByte = byteAddr%16;
/*     if(numBlock > 1)
    {
        uint8_t numSector = (numBlock - 2)/3 + 1;
        numBlock = numSector*4 + (numBlock - 2)%3;
    }
    else numBlock++; */
    if(!mifareclassic_AuthenticateBlock (_uid, uidLength, numBlock, 1, keyuniversal)) //authen the block
    {
        fS50found =0; //if failed to authenticate try to research a mifare card.
        return -3;
    }
    if(!mifareclassic_ReadDataBlock(numBlock, data)) //read block
        return -4;
    return data[numByte]; //return data
}


/**************************************************************************/
/*! 
    @brief  read string from data block, the end address should be with the rage
            of 0 - 752, from Bytes 0 of block 1 to Byte 16 of block 62 

    @param  byteAddr    the address of the data to read. 16*47 data bytes  
    
    @returns   -1   if address is without the range
               -2   if failed to find a Mifare Classic card card
               -3   if authentication failed
               -4   if failed to read block
               data if succeed
*/
/**************************************************************************/
int DFRNFC::readBytes(uint8_t* buff, unsigned int byteAddrStart, unsigned int length)
{  
    int byteAddrEnd = byteAddrStart +length -1; 
    if(byteAddrEnd > 751)
       return -1;   // without range
    if(!fS50found)  // if no s50 card has been find
    {
       if(!readPassiveTargetID(PN532_MIFARE_ISO14443A, _uid, &uidLength)) //try to find one
           return -2;
    }
    uint8_t numBlockStart = DataBlockAddr[byteAddrStart/16];
    uint8_t numByteStart = byteAddrStart%16;
    uint8_t numBlockEnd = DataBlockAddr[byteAddrEnd/16];
    uint8_t numByteEnd = byteAddrEnd%16;
    uint8_t completeBlockCount = 0;
    
    for(uint8_t numBlock=numBlockStart;numBlock<=numBlockEnd;numBlock++)
    {
        if(numBlock == numBlockStart)
        {
            if(!mifareclassic_AuthenticateBlock (_uid, uidLength, numBlockStart, 1, keyuniversal)) //authen the block
            {
                fS50found =0; //if failed to authenticate try to research a mifare card.
                return -3;
            }
            if(!mifareclassic_ReadDataBlock(numBlockStart, data)) //read block
                return -4;
            memcpy(buff,data+numByteStart,16-numByteStart);
        }
        else if (numBlock == numBlockEnd)
        {
            if(!mifareclassic_AuthenticateBlock (_uid, uidLength, numBlockEnd, 1, keyuniversal)) //authen the block
            {
                fS50found =0; //if failed to authenticate try to research a mifare card.
                return -3;
            }
            if(!mifareclassic_ReadDataBlock(numBlockEnd, data)) //read block
                return -4;
            memcpy(buff+length-numByteEnd-1,data,numByteEnd+1);
        } 
        else if(isDataBlock[numBlock])
        {
            if(!mifareclassic_AuthenticateBlock (_uid, uidLength, numBlock, 1, keyuniversal)) //authen the block
            {
                fS50found =0; //if failed to authenticate try to research a mifare card.
                return -3;
            }
            if(!mifareclassic_ReadDataBlock(numBlock, data)) //read block
                return -4;
            memcpy(buff+completeBlockCount*16+16-numBlockStart+1,data,16);
            completeBlockCount++;
        }
    }
    return 1;

}


/**************************************************************************/
/*! 
    @brief  write bytes to card according to the address, the address should be with the rage
            of 0 - 752, from Bytes 0 of block 1 to Byte 16 of block 62 

    @param  byteAddr    the address of the data to read. 16*47 data bytes  
    
    @returns   -1   if address is without the range
               -2   if failed to find a Mifare Classic card card
               -3   if authentication failed
               -4   if failed to read the block
               -5   if failed to write the block
               1    if succeed
*/
/**************************************************************************/
int DFRNFC::write(unsigned int byteAddr,uint8_t byteData)
{
    if(byteAddr> 751)
        return -1;  // without the range
    if(!fS50found)  // if no s50 card has been find
    {
       if(!readPassiveTargetID(PN532_MIFARE_ISO14443A, _uid, &uidLength)) //try to find one
           return -2;
    }
    uint8_t numBlock = DataBlockAddr[byteAddr/16];
    uint8_t numByte = byteAddr%16;
/*     if(numBlock > 1)
    {
        uint8_t numSector = (numBlock - 2)/3 + 1;
        numBlock = numSector*4 + (numBlock - 2)%3;
    }
    else numBlock++; */
    if(!mifareclassic_AuthenticateBlock (_uid, uidLength, numBlock, 1, keyuniversal))  //authen the block
    {   
        fS50found = 0;
        return -3;
    }
    if(!mifareclassic_ReadDataBlock(numBlock, data)) //read block
        return -4;
    data[numByte] = byteData;                        //write the data
    if(!mifareclassic_WriteDataBlock(numBlock,data)) //write the block
        return -5;
    return 1;
}


/**************************************************************************/
/*! 
    @brief  write string  to data block, the end address should be with the rage
            of 0 - 752, from Bytes 0 of block 1 to Byte 16 of block 62 

    @param  byteAddr    the address of the data to read. 16*47 data bytes  
    
    @returns   -1   if address is without the range
               -2   if failed to find a Mifare Classic card card
               -3   if authentication failed
               -4   if failed to read block
               data if succeed
*/
/**************************************************************************/
int DFRNFC::writeBytes(uint8_t* buff, unsigned int byteAddrStart, unsigned int length)
{  
    int byteAddrEnd = byteAddrStart +length -1; 
    if(byteAddrEnd > 751)
       return -1;   // without range
    if(!fS50found)  // if no s50 card has been find
    {
       if(!readPassiveTargetID(PN532_MIFARE_ISO14443A, _uid, &uidLength)) //try to find one
           return -2;
    }
    uint8_t numBlockStart = DataBlockAddr[byteAddrStart/16];
    uint8_t numByteStart = byteAddrStart%16;
    uint8_t numBlockEnd = DataBlockAddr[byteAddrEnd/16];
    uint8_t numByteEnd = byteAddrEnd%16;
    uint8_t completeBlockCount = 0;
    
    for(uint8_t numBlock=numBlockStart;numBlock<=numBlockEnd;numBlock++)
    {
        if(numBlock == numBlockStart)
        {
            if(!mifareclassic_AuthenticateBlock (_uid, uidLength, numBlockStart, 1, keyuniversal)) //authen the block
            {
                fS50found =0; //if failed to authenticate try to research a mifare card.
                return -3;
            }
            if(!mifareclassic_ReadDataBlock(numBlockStart, data)) //read block
                return -4;
            memcpy(data+numByteStart,buff,16-numByteStart);
            if(!mifareclassic_WriteDataBlock(numBlock,data)) //write the block
                return -5;
        }
        else if (numBlock == numBlockEnd)
        {
            if(!mifareclassic_AuthenticateBlock (_uid, uidLength, numBlockEnd, 1, keyuniversal)) //authen the block
            {
                fS50found =0; //if failed to authenticate try to research a mifare card.
                return -3;
            }
            if(!mifareclassic_ReadDataBlock(numBlockEnd, data)) //read block
                return -4;
            memcpy(data,buff+length-numByteEnd-1,numByteEnd+1);
            if(!mifareclassic_WriteDataBlock(numBlockEnd,data)) //write the block
                return -5;
        } 
        else if(isDataBlock[numBlock])
        {
            if(!mifareclassic_AuthenticateBlock (_uid, uidLength, numBlock, 1, keyuniversal)) //authen the block
            {
                fS50found =0; //if failed to authenticate try to research a mifare card.
                return -3;
            }
            if(!mifareclassic_ReadDataBlock(numBlock, data)) //read block
                return -4;
            memcpy(data,buff+completeBlockCount*16+16-numBlockStart+1,16);
            completeBlockCount++;
            if(!mifareclassic_WriteDataBlock(numBlock,data)) //write the block
                return -5;
        }
    }
    return 1;

}

/**************************************************************************/
/*! 
    @brief  try to find the PN532& Mifare Classic card

    @returns   -1   if failed to find pn532
               -2   if failed to find a Mifare Classic card
               1    if succeed
*/
/**************************************************************************/
int DFRNFC::availinfo()
{
    uint8_t version[4];
    SAMConfig();
    if(!getFirmwareVersion(version))
    {
        _serial->println("version failed");
        return -1;
    }
    
    if(!readPassiveTargetID(PN532_MIFARE_ISO14443A, _uid, &uidLength))
    {
        _serial->println("card failed");
        return -2;
    }    
      // Got ok data, print it out!
    _serial->print("Found chip PN5"); _serial->println(version[0],HEX);
    _serial->print("Firmware ver."); _serial->print(version[1],DEC);
    _serial->print('.'); _serial->println(version[2], DEC);
    return 1;
}

/**************************************************************************/
/*! 
    @brief  try to find the PN532& Mifare Classic card

    @returns   -1   if failed to find pn532
               -2   if failed to find a Mifare Classic card
               1    if succeed
*/
/**************************************************************************/
int DFRNFC::available()
{
    uint8_t version[4];
    SAMConfig();
    if(!getFirmwareVersion(version))
        return -1;    
    if(!readPassiveTargetID(PN532_MIFARE_ISO14443A, _uid, &uidLength))
        return -2;  
    return 1;
}


/**************************************************************************/
/*! 
    @brief  try to dump the Mifare Classic card mem
*/
/**************************************************************************/
void DFRNFC::memdump(void)
{
    _serial->println("Start memdump");
    for(int i=0;i<64;i++)
    {
      if(!mifareclassic_AuthenticateBlock (_uid, uidLength, i, 1, keyuniversal))
      {
        _serial->print("Block ");_serial->print(i,DEC);_serial->print(":  ");
        _serial->println("failed to authen");
      }
      else
      {
        if(!mifareclassic_ReadDataBlock(i,data))
        {
          _serial->print("Block ");_serial->print(i,DEC);_serial->print(":  ");
          _serial->println("failed to read");
        }
        else
        {
          _serial->print("Block "); _serial->print(i,DEC);_serial->print(":  ");
          PrintHexChar(data,16);
        }
      }
    }
}

/**************************************************************************/
/*! 
    @brief  Prints a hexadecimal value in plain characters

    @param  data      Pointer to the byte data
    @param  numBytes  Data length in bytes
*/
/**************************************************************************/
void DFRNFC::PrintHex(const byte * data, const uint32_t numBytes)
{
  uint32_t szPos;
  for (szPos=0; szPos < numBytes; szPos++) 
  {
    _serial->print("0x");
    // Append leading 0 for small values
    if (data[szPos] <= 0xF)
      _serial->print("0");
    _serial->print(data[szPos], HEX);
    if ((numBytes > 1) && (szPos != numBytes - 1))
    {
      _serial->print(" ");
    }
  }
  _serial->println("");
}



/**************************************************************************/
/*! 
    @brief  Prints a hexadecimal value in plain characters, along with
            the char equivalents in the following format

            00 00 00 00 00 00  ......

    @param  data      Pointer to the byte data
    @param  numBytes  Data length in bytes
*/
/**************************************************************************/
void DFRNFC::PrintHexChar(const byte * data, const uint32_t numBytes)
{
  uint32_t szPos;
  for (szPos=0; szPos < numBytes; szPos++) 
  {
    // Append leading 0 for small values
    if (data[szPos] <= 0xF)
      _serial->print("0");
    _serial->print(data[szPos], HEX);
    if ((numBytes > 1) && (szPos != numBytes - 1))
    {
      _serial->print(" ");
    }
  }
  _serial->print("  ");
  for (szPos=0; szPos < numBytes; szPos++) 
  {
    if (data[szPos] <= 0x1F)
      _serial->print(".");
    else
      _serial->print(data[szPos]);
  }
  _serial->println("");
}
 