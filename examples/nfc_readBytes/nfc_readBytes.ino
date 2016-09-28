/***************************************************
      NFC Module for Arduino (SKU:DFR0231)
 <http://www.dfrobot.com/wiki/index.php/NFC_Module_for_Arduino_%28SKU:DFR0231%29>
 ***************************************************
 This example reads characters in the Mifare Classic of the
 data blocks (address 0 to address 751) using default keyB {0xFF,0xFF,0xFF,
 0xFF,0xFF,0xFF} and prints it to the computer. 
 
 Created 2015-3-9
 By Bill Hong 
 
 GNU Lesser General Public License. 
 See <http://www.gnu.org/licenses/> for details.
 All above must be included in any redistribution
 ****************************************************/

/***********Notice and Trouble shooting***************
 1.The address is from 0 to 751, 752 Bytes in the data blocks. 
 2.This code is tested on Arduino Uno.
 ****************************************************/
 
#include "Arduino.h"
#include "DFRNFC.h"

DFRNFC nfc; 

void setup(void)
{
  Serial.begin(115200); //PN532 default SerialBaudRate is 115200
 
  //initialize nfc module
  nfc.begin(Serial);
  Serial.println("Looking for PN532...");
}


void loop()
{
  uint8_t data[752];
  int startAddress = 0;
  int dataLength = 752;
  
  // read byte all bytes of the Mifare Classic Card to data;
  int success = nfc.readBytes(data,startAddress,dataLength);
  
  //returned information
  switch(success)
  {
    case -1:
      Serial.println("address is without range");
      break;
    case -2:
      Serial.println("failed to find a Mifare classic card");
      break;
    case -3:
      Serial.println("faild to authenticate, the device will try to find  a new card");
      break;
    case -4:
      Serial.println("faild to read");
      break;
    default:
      Serial.println();
      Serial.println("start printing the data");
      for(int i=0;i<752;i+=16)
      {
        nfc.PrintHexChar(data+i,16); //print data,16 bytes per line
      }
      break;
  }
  
  while(1);
  
}
