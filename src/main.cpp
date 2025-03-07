#include <Arduino.h>
#include <U8g2lib.h>
#include <bitset>
#include <STM32FreeRTOS.h>
#include <stm32l4xx_hal.h> 
#include "Knob.h"
#include <ES_CAN.h>

//Constants
  const uint32_t interval = 100; //Display update interval

//Pin definitions
  //Row select and enable
  const int RA0_PIN = D3;
  const int RA1_PIN = D6;
  const int RA2_PIN = D12;
  const int REN_PIN = A5;

  //Matrix input and output
  const int C0_PIN = A2;
  const int C1_PIN = D9;
  const int C2_PIN = A6;
  const int C3_PIN = D1;
  const int OUT_PIN = D11;

  //Audio analogue out
  const int OUTL_PIN = A4;
  const int OUTR_PIN = A3;

  //Joystick analogue in
  const int JOYY_PIN = A0;
  const int JOYX_PIN = A1;

  //Output multiplexer bits
  const int DEN_BIT = 3;
  const int DRST_BIT = 4;
  const int HKOW_BIT = 5;
  const int HKOE_BIT = 6;

//Display driver object
U8G2_SSD1305_128X32_ADAFRUIT_F_HW_I2C u8g2(U8G2_R0);

//Step Sizes
const uint32_t stepSizes [] = {51076056,54113197,57330935,60740010,64351798,68178356,72232452,76527617,81078186,85899345,91007186, 96418755};
const std::string notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
volatile uint32_t currentStepSize;

//Interupt timer
HardwareTimer sampleTimer(TIM1);

//Global system state struct
struct {
  std::bitset<32> inputs;
  SemaphoreHandle_t mutex;
  Knob knob0{0};
  Knob knob1{1};
  Knob knob2{2};
  Knob knob3{3};
} sysState;

Knob* knobs[] = {&sysState.knob0, &sysState.knob1, &sysState.knob2, &sysState.knob3};

//Function to set outputs using key matrix
void setOutMuxBit(const uint8_t bitIdx, const bool value) {
      digitalWrite(REN_PIN,LOW);
      digitalWrite(RA0_PIN, bitIdx & 0x01);
      digitalWrite(RA1_PIN, bitIdx & 0x02);
      digitalWrite(RA2_PIN, bitIdx & 0x04);
      digitalWrite(OUT_PIN,value);
      digitalWrite(REN_PIN,HIGH);
      delayMicroseconds(2);
      digitalWrite(REN_PIN,LOW);
}

//Function to read inputs from switch matrix columns
std::bitset<4> readCols(){
  std::bitset<4> result;
  result[0] = digitalRead(C0_PIN);
  result[1] = digitalRead(C1_PIN);
  result[2] = digitalRead(C2_PIN);
  result[3] = digitalRead(C3_PIN);
  return result;

}

void setRow(uint8_t rowIdx){
  digitalWrite(REN_PIN, LOW);
  digitalWrite(RA0_PIN, rowIdx & 0x01);
  digitalWrite(RA1_PIN, rowIdx & 0x02);
  digitalWrite(RA2_PIN, rowIdx & 0x04);
  digitalWrite(REN_PIN, HIGH);
}

void setISR(){
  static uint32_t phaseAcc = 0;
  int k2r = __atomic_load_n(&sysState.knob2.rotation, __ATOMIC_ACQUIRE);   
  if(k2r > 3){
    phaseAcc = phaseAcc + (currentStepSize << (k2r - 4));
  }
  else{
    phaseAcc = phaseAcc + (currentStepSize >> (4 - k2r));
  }
  int32_t Vout = (phaseAcc >> 24) - 128;
  int k3r = __atomic_load_n(&sysState.knob3.rotation, __ATOMIC_ACQUIRE); 
  Vout = Vout >> (8 - k3r);

  analogWrite(OUTR_PIN, Vout + 128);
}
  
void scanKeysTask(void * pvParameters) {
  
  const TickType_t xFrequency = 20/portTICK_PERIOD_MS;
  TickType_t xLastWakeTime = xTaskGetTickCount();
  static int lastIncrement;
  std::bitset<4> cols;
  uint8_t TX_Message[8] = {0};

  for(;;){ 
    uint32_t localCurrentStepSize{0};
    vTaskDelayUntil( &xLastWakeTime, xFrequency );
    xSemaphoreTake(sysState.mutex, portMAX_DELAY);
    for(int i=0;i<5;i++){
      setRow(i);
      delayMicroseconds(3);
      cols = readCols();
      if(i<3){
        for(int j=0;j<4;j++){
          if(cols[j] != sysState.inputs[(i*4 + j)]){
            if(cols[j] == 0){
              TX_Message[0] = 'P'; //Pressed
            }
            else{
              TX_Message[0] = 'K'; //Released
            }
            TX_Message[1] = sysState.knob2.rotation; //Octave
            TX_Message[2] = (i*4 + j); //Note Number
          }
        }
      }
      CAN_TX(0x123, TX_Message);
      sysState.inputs.set(i*4, readCols()[0]);
      sysState.inputs.set(i*4+1, readCols()[1]);
      sysState.inputs.set(i*4+2, readCols()[2]);
      sysState.inputs.set(i*4+3, readCols()[3]);
    }
    for(int i=0;i<12;i++){
      if(sysState.inputs[i]==0){
        localCurrentStepSize = stepSizes[i];
      }
    }
    for(int i=0;i<4;i++){
      int currentStateA = sysState.inputs[(12 + (3-i)*2)];
      int currentStateB = sysState.inputs[(13 + (3-i)*2)];
      knobs[i]->updateValues(currentStateA, currentStateB);
    }
    xSemaphoreGive(sysState.mutex);
    __atomic_store_n(&currentStepSize, localCurrentStepSize, __ATOMIC_RELAXED);
  }  
}

void displayUpdateTask(void * pvParameters) {
  const TickType_t xFrequency = 100/portTICK_PERIOD_MS;
  TickType_t xLastWakeTime = xTaskGetTickCount();
  uint32_t ID;
  uint8_t RX_Message[8]={0};

  while (1) {
    vTaskDelayUntil( &xLastWakeTime, xFrequency );
      //Update display
    u8g2.clearBuffer();         // clear the internal memory
    u8g2.setFont(u8g2_font_ncenB08_tr); // choose a suitable font
    //u8g2.drawStr(2,10,"Hello World!");  // write something to the internal memory
    xSemaphoreTake(sysState.mutex, portMAX_DELAY);
    char cstr0[2], cstr1[2], cstr2[2], cstr3[2];
    snprintf(cstr0, sizeof(cstr0), "%d", sysState.knob0.rotation);
    snprintf(cstr1, sizeof(cstr1), "%d", sysState.knob1.rotation);
    snprintf(cstr2, sizeof(cstr2), "%d", sysState.knob2.rotation);
    snprintf(cstr3, sizeof(cstr3), "%d", sysState.knob3.rotation);

    while (CAN_CheckRXLevel())
	    CAN_RX(ID, RX_Message);

    u8g2.drawStr(2,10, cstr0);
    u8g2.drawStr(20,10, cstr1);
    u8g2.drawStr(38,10, cstr2);
    u8g2.drawStr(56,10, cstr3);  

    u8g2.setCursor(66,30);
    u8g2.print((char) RX_Message[0]);
    u8g2.print(RX_Message[1]);
    u8g2.print(RX_Message[2]);

    u8g2.setCursor(2,20);
    u8g2.print(sysState.inputs.to_ulong(),HEX);
    xSemaphoreGive(sysState.mutex);
    
    u8g2.sendBuffer();          // transfer internal memory to the display

    //Toggle LED
    digitalToggle(LED_BUILTIN);
  }
}

void setup() {
  // put your setup code here, to run once:
  sysState.knob2.rotation = 4;

  //Set pin directions
  pinMode(RA0_PIN, OUTPUT);
  pinMode(RA1_PIN, OUTPUT);
  pinMode(RA2_PIN, OUTPUT);
  pinMode(REN_PIN, OUTPUT);
  pinMode(OUT_PIN, OUTPUT);
  pinMode(OUTL_PIN, OUTPUT);
  pinMode(OUTR_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(C0_PIN, INPUT);
  pinMode(C1_PIN, INPUT);
  pinMode(C2_PIN, INPUT);
  pinMode(C3_PIN, INPUT);
  pinMode(JOYX_PIN, INPUT);
  pinMode(JOYY_PIN, INPUT);

  //Initialise display
  setOutMuxBit(DRST_BIT, LOW);  //Assert display logic reset
  delayMicroseconds(2);
  setOutMuxBit(DRST_BIT, HIGH);  //Release display logic reset
  u8g2.begin();
  setOutMuxBit(DEN_BIT, HIGH);  //Enable display power supply

  CAN_Init(true);
  setCANFilter(0x123,0x7ff);
  CAN_Start();

  //Initialise UART
  Serial.begin(9600);
  Serial.println("Hello World");

  sampleTimer.setOverflow(22000, HERTZ_FORMAT);
  sampleTimer.attachInterrupt(setISR);
  sampleTimer.resume();
  sysState.mutex = xSemaphoreCreateMutex();
  TaskHandle_t scanKeysHandle = NULL;
  xTaskCreate(
  scanKeysTask,		/* Function that implements the task */
  "scanKeys",		/* Text name for the task */
  64,      		/* Stack size in words, not bytes */
  NULL,			/* Parameter passed into the task */
  1,			/* Task priority */
  &scanKeysHandle);	/* Pointer to store the task handle */
  TaskHandle_t displayUpdateHandle = NULL;
  xTaskCreate(
  displayUpdateTask,		/* Function that implements the task */
  "displayUpdate",		/* Text name for the task */
  256,      		/* Stack size in words, not bytes */
  NULL,			/* Parameter passed into the task */
  1,			/* Task priority */
  &displayUpdateHandle);	/* Pointer to store the task handle */
  vTaskStartScheduler();
}

void loop() {
}