#include "Display_ST7789.h"
#include "PlayerCore.h"

SPIClass LCDspi(FSPI);
void SPI_Init()
{
  LCDspi.begin(EXAMPLE_PIN_NUM_SCLK,EXAMPLE_PIN_NUM_MISO,EXAMPLE_PIN_NUM_MOSI);
}

void LCD_WriteCommand(uint8_t Cmd)
{
  LCDspi.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, LOW);
  LCDspi.transfer(Cmd);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  LCDspi.endTransaction();
}
void LCD_WriteData(uint8_t Data)
{
  LCDspi.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
  LCDspi.transfer(Data);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  LCDspi.endTransaction();
}
void LCD_WriteData_Word(uint16_t Data)
{
  LCDspi.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
  LCDspi.transfer16(Data);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  LCDspi.endTransaction();
}
void LCD_WriteData_nbyte(uint8_t* SetData,uint8_t* ReadData,uint32_t Size)
{
  LCDspi.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
  if (ReadData) LCDspi.transferBytes(SetData, ReadData, Size);
  else LCDspi.writeBytes(SetData, Size); // Write-only LCD; no receive transaction.
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  LCDspi.endTransaction();
}

void LCD_Reset(void)
{
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  delay(50);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_RST, LOW);
  delay(50);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_RST, HIGH);
  delay(50);
}
void LCD_Init(void)
{
  pinMode(EXAMPLE_PIN_NUM_LCD_CS, OUTPUT);
  pinMode(EXAMPLE_PIN_NUM_LCD_DC, OUTPUT);
  pinMode(EXAMPLE_PIN_NUM_LCD_RST, OUTPUT);
  SPI_Init();

  LCD_Reset();
  //************* Start Initial Sequence **********//

  delay(120);
  LCD_WriteCommand(0x29);    //Display on
  delay(120);
  LCD_WriteCommand(0x11);
  delay(120);                //ms
  LCD_WriteCommand(0x36);
  LCD_WriteData(0x00);

  LCD_WriteCommand(0x3A);
  LCD_WriteData(0x55); // Explicit RGB565 format, as in the factory ESP-IDF driver.

  LCD_WriteCommand(0xB0);
  LCD_WriteData(0x00);
  LCD_WriteData(0xE0); // Clear RAMCTRL ENDIAN bit 3: MSB-first, matching LV_COLOR_16_SWAP=1.

  LCD_WriteCommand(0xB2);
  LCD_WriteData(0x0C);
  LCD_WriteData(0x0C);
  LCD_WriteData(0x00);
  LCD_WriteData(0x33);
  LCD_WriteData(0x33);

  LCD_WriteCommand(0xB7);
  LCD_WriteData(0x75);   //VGH=14.97V,VGL=-7.67V

  LCD_WriteCommand(0xBB);
  LCD_WriteData(0x1A);

  LCD_WriteCommand(0xC0);
  LCD_WriteData(0x2C);

  LCD_WriteCommand(0xC2);
  LCD_WriteData(0x01);
  LCD_WriteData(0xFF);

  LCD_WriteCommand(0xC3);
  LCD_WriteData(0x13);

  LCD_WriteCommand(0xC4);
  LCD_WriteData(0x20);

  LCD_WriteCommand(0xC6);
  LCD_WriteData(0x0F);

  LCD_WriteCommand(0xD0);
  LCD_WriteData(0xA4);
  LCD_WriteData(0xA1);

  LCD_WriteCommand(0xD6);
  LCD_WriteData(0xA1);

  LCD_WriteCommand(0xE0);
  LCD_WriteData(0xD0);
  LCD_WriteData(0x0D);
  LCD_WriteData(0x14);
  LCD_WriteData(0x0D);
  LCD_WriteData(0x0D);
  LCD_WriteData(0x09);
  LCD_WriteData(0x38);
  LCD_WriteData(0x44);
  LCD_WriteData(0x4E);
  LCD_WriteData(0x3A);
  LCD_WriteData(0x17);
  LCD_WriteData(0x18);
  LCD_WriteData(0x2F);
  LCD_WriteData(0x30);

  LCD_WriteCommand(0xE1);
  LCD_WriteData(0xD0);
  LCD_WriteData(0x09);
  LCD_WriteData(0x0F);
  LCD_WriteData(0x08);
  LCD_WriteData(0x07);
  LCD_WriteData(0x14);
  LCD_WriteData(0x37);
  LCD_WriteData(0x44);
  LCD_WriteData(0x4D);
  LCD_WriteData(0x38);
  LCD_WriteData(0x15);
  LCD_WriteData(0x16);
  LCD_WriteData(0x2C);
  LCD_WriteData(0x2E);

  LCD_WriteCommand(0x21);

  LCD_WriteCommand(0x29);

  LCD_WriteCommand(0x2C);

  Touch_Init();
}
/******************************************************************************
function: Set the cursor position
parameter :
    Xstart:   Start uint16_t x coordinate
    Ystart:   Start uint16_t y coordinate
    Xend  :   End uint16_t coordinates
    Yend  :   End uint16_t coordinatesen
******************************************************************************/
void LCD_SetCursor(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t  Yend)
{
  if (HORIZONTAL) {
    // set the X coordinates
    LCD_WriteCommand(0x2A);
    LCD_WriteData(Xstart >> 8);
    LCD_WriteData(Xstart + Offset_X);
    LCD_WriteData(Xend >> 8);
    LCD_WriteData(Xend + Offset_X);

    // set the Y coordinates
    LCD_WriteCommand(0x2B);
    LCD_WriteData(Ystart >> 8);
    LCD_WriteData(Ystart + Offset_Y);
    LCD_WriteData(Yend >> 8);
    LCD_WriteData(Yend + Offset_Y);
  }
  else {
    // set the X coordinates
    LCD_WriteCommand(0x2A);
    LCD_WriteData(Ystart >> 8);
    LCD_WriteData(Ystart + Offset_Y);
    LCD_WriteData(Yend >> 8);
    LCD_WriteData(Yend + Offset_Y);
    // set the Y coordinates
    LCD_WriteCommand(0x2B);
    LCD_WriteData(Xstart >> 8);
    LCD_WriteData(Xstart + Offset_X);
    LCD_WriteData(Xend >> 8);
    LCD_WriteData(Xend + Offset_X);
  }
  LCD_WriteCommand(0x2C);
}
/******************************************************************************
function: Refresh the image in an area
parameter :
    Xstart:   Start uint16_t x coordinate
    Ystart:   Start uint16_t y coordinate
    Xend  :   End uint16_t coordinates
    Yend  :   End uint16_t coordinates
    color :   Set the color
******************************************************************************/
void LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend,uint16_t* color)
{
  if (!color || Xstart > Xend || Ystart > Yend || Xend >= LCD_WIDTH || Yend >= LCD_HEIGHT) return;
  uint16_t Show_Width = Xend - Xstart + 1;
  uint16_t Show_Height = Yend - Ystart + 1;
  uint32_t numBytes = Show_Width * Show_Height * sizeof(uint16_t);
  // Send the complete dirty rectangle in one transaction (previously twelve).
  // Pixel bytes are already MSB-first; preserve RAMCTRL 0xE0 and LVGL's swap.
  const uint8_t columns[] = {uint8_t(Xstart >> 8), uint8_t(Xstart), uint8_t(Xend >> 8), uint8_t(Xend)};
  const uint8_t rows[] = {uint8_t(Ystart >> 8), uint8_t(Ystart), uint8_t(Yend >> 8), uint8_t(Yend)};
  LCDspi.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, LOW); LCDspi.write(0x2A);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH); LCDspi.writeBytes(columns, sizeof(columns));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, LOW); LCDspi.write(0x2B);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH); LCDspi.writeBytes(rows, sizeof(rows));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, LOW); LCDspi.write(0x2C);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH); LCDspi.writeBytes(reinterpret_cast<uint8_t*>(color), numBytes);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  LCDspi.endTransaction();
}


// backlight
uint8_t LCD_Backlight = 50;
void Backlight_Init()
{
  ledcSetup(PWM_Channel, Frequency, Resolution);
  ledcAttachPin(LCD_Backlight_PIN, PWM_Channel);
  ledcWrite(PWM_Channel, Dutyfactor);
  Set_Backlight(LCD_Backlight);      //0~100
}

void Set_Backlight(uint8_t Light)
{
  if (Light > Backlight_MAX) Light = Backlight_MAX;
  static int previous = -1;
  if (previous == Light) return;
  previous = LCD_Backlight = Light;
  ledcWrite(PWM_Channel, player::backlightDuty(Light)); // 10-bit maximum is 1023.
}

void LCD_SetSleep(bool asleep) {
  static bool sleeping = false;
  static uint32_t sleptAt = 0;
  if (asleep == sleeping) return;
  if (asleep) {
    Set_Backlight(0);
    LCD_WriteCommand(0x28); // Display off, then controller sleep; touch stays powered.
    LCD_WriteCommand(0x10);
    sleptAt = millis();
  } else {
    const uint32_t elapsed = millis() - sleptAt;
    if (elapsed < 120) delay(120 - elapsed);
    LCD_WriteCommand(0x11);
    delay(120); // ST7789 sleep-out stabilization before drawing or display-on.
    LCD_WriteCommand(0x29);
  }
  sleeping = asleep;
}



