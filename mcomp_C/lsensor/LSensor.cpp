/*
 * LSensor.cpp
 *
 *  Created on: 25 Mar 2018
 *      Author: David Avery 15823926
 *      Author: Stephen Pope 15836791
 */

#include "../config/robot_config.h"
#include "LSensor.h"

//buffer of bytes that takes in a packet of four reads from the LiDAR sensor
byte * buffer;
unsigned int distances[360];
Waypoint wp[360];

LSensor::LSensor() {
  pDistances = nullptr;
  inByte = 0;
  avgRPM = 0;
  targetRPM = LiDAROptimalRPM;
  targetPWM = 71;  //71 rpm to hit as close as we can to the target rpm of 240
  AFMS1 = Adafruit_MotorShield(0x61);
  lidarMotor = (*AFMS1.getMotor(1));
  AFMS1.begin();
  lidarMotor.setSpeed(targetPWM);
  lidarMotor.run(FORWARD);
  SENSOR.begin(SENSORRATE);             //init the input from LiDAR serial2
  SENSOR.setTimeout(SENSORTIMEOUT);
  DEBUG.begin(DEBUGRATE);
  DEBUG.setTimeout(DEBUGTIMEOUT);
}

LSensor::~LSensor() {
  //lidarMotor.setSpeed(0);
}

unsigned int LSensor::getAvgRPM() {
  avgRPM = 0;
  unsigned int res;            //RPM result
  int counter = (2-2);//buffer is always missing first 2 bytes
  byte lowerB;  //nth byte in the buffer, lower half of 16 bit little-endian value
  byte upperB;  //n + 1 byte in the buffer, upper half of 16 bit little-endian value
  for (int i = 0; i < 90; i++) {
    res = 0;
    lowerB = buffer[counter];
    upperB = buffer[counter + 1];
    res = res | upperB;  //res (0) bitwise OR with b2 = b2 but in a 16 bit in not 8 bit byte
    res <<= 8;                       //shift bits 8 left to make space for b3
    res = res | lowerB;  //rpmLe (b2+8 zeros) logical OR with b3 = b2 concat b3 in one 16 bit value
    res >>= 6;  //Shift right 6 to remove the floating point (64th of an RPM) values
    DEBUG.println(res);
    avgRPM = avgRPM + res;
    counter = counter + 22;
  }
  avgRPM = avgRPM / 90;
  return avgRPM;                      //Returns RPM as whole number
}

/*
 * A single packet contains 4 reads.
 * A single read is 4 bytes long.
 * The first 14 bits(0-13) of the first 2 bytes are the actual distance data.
 * The remaining 2 bits are error states.
 * Bit 14 shows high if the return signal was weaker than expected.
 * Bit 15 shows high if the distance could not be calculated.
 * If bit 15 is high the read should be 0 and discounted.
 * The final 2 bytes are signal strength information and not worried about here.
 * The bit data for distance is stored in little endian format. Backwards to normal.
 * It needs to be flipped in order for the JVM on the other end to work with it.
 */
unsigned int LSensor::getRead(int location) {
  unsigned int res = 0;
  byte lowerByte = buffer[location];
  byte upperByte = buffer[location + 1];
  if (upperByte & 0x80) {  //Bitwise compare to see if did not calc flag was high
    return 0;
  }
  upperByte = upperByte & 0x3F;  //Removes weak signal flag
  res = res | upperByte;
  res <<= 8;
  res = res | lowerByte;
  return res;
}

unsigned int* LSensor::decodeRead() {
  int counter = -2;//first 2 are missed
  for (int i = 0; i < 90; i = i + 1) {
    counter = counter + 4;  //advance to first read
    for (int j = 0; j < 4; j++) {
      distances[i * 4 + j] = getRead(counter);
      counter = counter + 4;
    }

    counter = counter + 2;  //To jump over the checksum bytes!

  }
  pDistances = &distances[0];
  return pDistances;
}

bool LSensor::adjustRPM() {
  getAvgRPM();
  if (avgRPM > targetRPM + 5) {  //if RPM is more than 10RPM off target, adjust it
    targetPWM = targetPWM - 1;
    lidarMotor.setSpeed(targetPWM);
    lidarMotor.run(FORWARD);
    return false;
  } else if (avgRPM < targetRPM - 5) {
    targetPWM = targetPWM + 1;
    lidarMotor.setSpeed(targetPWM);
    lidarMotor.run(FORWARD);
    return false;
  } else {
    return true;
  }
}

void LSensor::getEncodedRead() {

  while (SENSOR.available()) {  //empty the buffer
    SENSOR.read();
  }

  bool complete = false;
  while (!complete) {

    while (!SENSOR.available()) {  //wait for data
    }

    inByte = SENSOR.read();
    if (inByte == 0xFA) {            //Read a byte from DEBUG
      //buffer[0] = 0xFA;  //Read the next bit in the serial and write it to next position in buffer
      DEBUG.println("Found 0xFA");
      inByte = SENSOR.read();
      if (inByte == 0xA0) {            //Read a byte from DEBUG
        //buffer[1] = 0xA0;  //Read the next bit in the serial and write it to next position in buffer
        DEBUG.println("Found 0xA0");
        SENSOR.readBytes(buffer, 1978);//fix for first?
//        for (int i = 2; i < 1980; i++) {       //The head of a LiDAR packet read
//          while (!SENSOR.available()) {  //wait for data
//          }
//          buffer[i] = SENSOR.read();  //Read the next bit in the serial and write it to next position in buffer
//        }
        complete = true;
      }
    }
  }
}

Waypoint * LSensor::toWaypoint() {
//distance is 14bits
//distance is in mm so max =16,383mm
//convert to cm so dave can just div by 5 to make grid squares
//so div by 10
//then pos is array is theta, so the we can calc the y value using trig
//the hyp and the opp gives us 2 sides of a triangle so we can SOHCAHTOA to find x
//Waypoint constructor does this when handed an AngleDistance
  DEBUG.println("Start DATA: (T , D) - (X , Y)");
  for (int i = 0; i < 360; i++) {
    AngleDistance ad = AngleDistance((double) i,
                                     (long) (*(pDistances + i) / 10));

    DEBUG.print(ad.getTheta());
    DEBUG.print(" , ");
    DEBUG.print(ad.getDistance());
    DEBUG.print(" - ");

    Waypoint a = Waypoint(ad);
    wp[i] = a;
    DEBUG.print(a.getX());
    DEBUG.print(" , ");
    DEBUG.println(a.getY());
  }
  Waypoint* wpPtr = &wp[0];  //point to head of WP array
  return wpPtr;
}

Waypoint * LSensor::sense() {
  buffer = new byte[1980];

  getEncodedRead();  //So we can dig out an accurate avgRPM
  while (adjustRPM() == false) {  //Keep adjusting RPM until within 10 of target
    getEncodedRead();
    adjustRPM();
  }
  getEncodedRead();  //The proper read
  decodeRead();  //Reverse reads so they are BigEndian and return pointer to head of array
  delete(buffer);
  return toWaypoint();
}

Waypoint * LSensor::lSensorTest() {
//TODO have a test buff
//  getEncodedRead();
//  getAvgRPM();
////  getEncodedRead();
////  decodeRead();
//  sense();
  return 0;//toWaypoint();
}
