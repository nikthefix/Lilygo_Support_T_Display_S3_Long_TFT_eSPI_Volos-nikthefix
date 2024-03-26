/*
  For Lilygo T-Display-S3-Long 
  nikthefix - 20th Dec 2023

  Updated on 26th March 2024 to fix spurious touch response.

  Modified driver code and example sketch using TFT_eSPI in 'sprite only mode' 

  Calculator GUI and implementation by Volos Projects.

  Versions:
  TFT_eSPI 2.5.34 - latest at time of writing
  ESP32 Arduino 3.0.0-alpha3 - latest at time of writing

  Notes:
  As the display doesn't implement a scan orientation hardware rotate - as far as I can see from the current datasheet - we need to use 
  a soft matrix rotation to get a landscape view without messing with TFT_eSPI. This is implemented in lcd_PushColors_rotated_90().
  You can then use lcd_setRotation(2) which IS hardware implemented, to flip the whole thing upside down if you need.
  In this case you would need to manually flip your touch coordinates in getTouch()
  Code has been stripped down to support QSPI display only.

  Build Options:
  Board  ESP32-S3-Dev
  USB CDC On boot Enabled
  Flash Size 16MB
  Partition Scheme 16M Flash(3MB APP/9.9MB FATFS)
  PSRAM "OPI PSRAM"

  Since ESP32 Arduino 3.0.0-alpha3 is still pretty funky with a lot of existing Arduino driver code it may be necessary to downgrade to V2.xx as the project expands - in the short term

  ToDo: 
  
  Tidy up display driver code. Much of what's there is redundant unless you decide to use it. But the compiler knows what's not needed so there is no penalty for keeping it there.
  This demo currently uses a full screen buffer/sprite to update the whole display at about 15FPS with SPI at 30Mhz. I will update the driver to allow a partial refresh for much faster animations etc.
  This has already been implemented in my LVGL odometer example and it makes a huge difference. I will migrate those changes to the TFT_eSPI scenario asap.

  
*/


#include "AXS15231B.h"
#include <TFT_eSPI.h>
#include <Wire.h>
#include "pins_config.h"
#include "fontM.h"
#include "fontH.h"
#include "fontS.h"
#include "fontT.h"
#include "yt.h"
// For bitmap encoding: use Image2lcd, 16bit true colour, MSB First, RGB565, don't include head data, be sure to set max image size, save as .h file.

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);
uint8_t ALS_ADDRESS = 0x3B;
uint8_t read_touchpad_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x8};
int tx=0;
int ty=0;
int cx=-1;
int cy=-1;
int xpos[4]={4,48,92,136};
int ypos[4]={4,48,92,136};
String btns[4][4]={{"7","8","9","/"},{"4","5","6","*"},{"1","2","3","-"},{"0",".","=","+"}};
String num="";
int deb=0;
int operation=0;
float numBuf=0;
bool touch_held=false;
#define time_out_reset 30000
uint16_t touch_timeout=0;
uint16_t cnt=0;

//colors
unsigned short col1=0x39C7;
unsigned short col2=0x2945;
unsigned short col3=TFT_ORANGE;
unsigned short col4=TFT_SILVER;
unsigned short cls[4][4]={{col1,col1,col1,col2},{col1,col1,col1,col2},{col1,col1,col1,col2},{col1,col2,col2,col2}}; 
unsigned short tcls[4][4]={{col4,col4,col4,col3},{col4,col4,col4,col3},{col4,col4,col4,col3},{col4,col3,col3,col3}}; 

   
void draw()
{
 sprite.fillSprite(TFT_BLACK);
  //sprite.drawString(String(cx)+" "+String(tx),460,8,2);
  //sprite.drawString(String(cy)+" "+String(ty),460,24,2);
 //sprite.drawString(String(cy)+" "+String(ty)+"   "+String(n),200,20,2);

 sprite.loadFont(fontM);
 sprite.setTextDatum(0);
 sprite.fillRoundRect(190,48,460,106,2,col2);
 sprite.setTextColor(TFT_ORANGE,TFT_BLACK);
 sprite.drawString("CLEAR",576,8);
 sprite.fillRect(556,34,80,6,TFT_BLUE);
 
 sprite.setTextDatum(4);
 for(int i=0;i<4;i++)
 for(int j=0;j<4;j++){
 sprite.setTextColor(tcls[i][j],cls[i][j]);
 sprite.fillRoundRect(xpos[j],ypos[i],40,40,4,cls[i][j]);
 sprite.drawString(btns[i][j],xpos[j]+20,ypos[i]+20,4);
 if(cx==j && cy==i)
 sprite.fillCircle(xpos[j]+8,ypos[i]+8,4,TFT_RED);
 }
 
 sprite.unloadFont();
 sprite.setTextDatum(0);
 sprite.loadFont(fontT);
 sprite.setTextColor(TFT_SILVER,TFT_BLACK);
 sprite.drawString("T-DISPLAY S3 LONG",190,8);

 sprite.unloadFont();
 sprite.loadFont(fontH);
 sprite.setTextColor(TFT_SILVER,col2);

 bool lastDot=false;
 if(num.charAt(num.length()-1)=='.')
 lastDot=true;

 if(num!="" && lastDot==0){
 int nn=num.toInt();
 float nl=num.toFloat()*1000;
 if((nl-(nn*1000))==0)
 sprite.drawString(String(nn),210,58);
 else
 sprite.drawString(num,210,58);
 sprite.unloadFont();
 }
 if(lastDot)
 sprite.drawString(num,210,58);

 sprite.loadFont(fontS);
 sprite.setTextColor(col3,TFT_BLACK);
 sprite.drawString("NikTheFix ",190,161);

 sprite.setTextColor(0x8410,TFT_BLACK);
 sprite.drawString("VOLOS PROJECTS ",494,161);
 sprite.pushImage(604,158,30,20,yt);
 lcd_PushColors_rotated_90(0, 0, 640, 180, (uint16_t*)sprite.getPointer());

}

    
void setup() {
    pinMode(TOUCH_INT, INPUT_PULLUP);
    sprite.createSprite(640, 180);    // full screen landscape sprite in psram
    sprite.setSwapBytes(1);

    // comment out if using variable brightness
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);


    // if using esp32 3.0.0-alpha3 then you can use the following 2 lines to adjust the backlight brightness
    //ledcAttach(TFT_BL, 10000, 8);// pin, pwm freq, resolution in bits
    //ledcWrite(TFT_BL, 30);// pin, pulse width (a.k.a brightness 0-255)

    

    //ini touch screen 
    pinMode(TOUCH_RES, OUTPUT);
    digitalWrite(TOUCH_RES, HIGH);delay(2);
    digitalWrite(TOUCH_RES, LOW);delay(10);
    digitalWrite(TOUCH_RES, HIGH);delay(2);
    Wire.begin(TOUCH_IICSDA, TOUCH_IICSCL);

    //init display 
    axs15231_init();
    draw();
    
}

void getTouch()
{
    uint8_t buff[20] = {0};
    Wire.beginTransmission(ALS_ADDRESS);
    Wire.write(read_touchpad_cmd, 8);
    Wire.endTransmission();
    Wire.requestFrom(ALS_ADDRESS, 8);
    while (!Wire.available());
    Wire.readBytes(buff, 8);

    int pointX=-1;
    int pointY=-1;
    int type = 0;

    type = AXS_GET_GESTURE_TYPE(buff);
    pointX = AXS_GET_POINT_X(buff,0);
    pointY = AXS_GET_POINT_Y(buff,0);

        if(pointX > 640) pointX = 640;
        if(pointY > 180) pointY = 180;

        
        tx=map(pointX,627,10,0,640);
        ty=map(pointY,180,0,0,180);
        
        if(tx>180 && tx<590) return;  //mask invalid touch area
        if(ty>50 && tx>590) return;   //mask invalid tough area

        for(int i=0;i<4;i++)
        {if(tx>xpos[i] && tx<xpos[i]+44)
        cx=i;
        if(ty>ypos[i] && ty<ypos[i]+44)
        cy=i;}

        if(tx>=590 && tx<=640 && ty>=0 && ty<=50)
        {num=""; numBuf=0; operation=0; cx=-1; cy=-1;}

    if (cx>=0 && cx<4 && cy>=0 && cy<4 ) {
        String cs=btns[cy][cx];
        
        if(cs=="1" || cs=="2" || cs=="3" || cs=="4" || cs=="5" || cs=="6" || cs=="7" || cs=="8" || cs=="9" || cs=="0")
        {num=num+cs; if(num.length()>7) {num=""; numBuf=0; operation=0; cx=-1; cy=-1;}}

        if(cs==".")
        {
          bool finded=0;
          for(int i=0;i<num.length();i++)
          if(num.charAt(i)=='.')
          finded=true;

          if(!finded)
          num=num+cs;
        }

        if(cs=="+")
        {operation=1; numBuf=num.toFloat();
        num="";
        }   

        if(cs=="-")
        {operation=2; numBuf=num.toFloat();
        num="";
        } 

         if(cs=="*")
        {operation=3; numBuf=num.toFloat();
        num="";
        } 

         if(cs=="/")
        {operation=4; numBuf=num.toFloat();
        num="";
        } 

        if(cs=="=")
        {
          
        if(operation==1) 
        {numBuf=numBuf+num.toFloat();
        num=String(numBuf);}

        if(operation==2) 
        {numBuf=numBuf-num.toFloat();
        num=String(numBuf);}

        if(operation==3) 
        {numBuf=numBuf*num.toFloat();
        num=String(numBuf);}

        if(operation==4) 
        {numBuf=numBuf/num.toFloat();
        num=String(numBuf);}
        
        }        
    }     
}


void loop() 
{     
  if(digitalRead(TOUCH_INT)==LOW) 
    {
      if(touch_held==false)
        {
        getTouch();
        draw();
        }
      touch_held=true;
      touch_timeout=0;
    }    

  touch_timeout++;

  if(touch_timeout >= time_out_reset) 
    {
    touch_held=false;
    touch_timeout=time_out_reset;
    }   
}


