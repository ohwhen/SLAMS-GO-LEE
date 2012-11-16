#ifndef _GOLEE_CONFIG_H_
#define _GOLEE_CONFIG_H_

// Debug level
#define ERROR 1
#define LOG 2
#define INFO 3
#define DEBUG_LEVEL LOG

#define DEBUG 1

const int wifi_channel = 9;
const long wifi_rate = 2000000; // 2mbs, 1000000 - 54000000
const char ssid[] = "SIDLEE_AMS";
const char password[] = "";
const char server[] = "10.20.1.8"; // golee.sidlee.com
const int port = 9200;

// Memory
#define I2C_IN_BUFFER_SIZE 64
#define I2C_OUT_BUFFER_SIZE 64

// Pin setup
#define LATCH 8
#define CLOCK 2
#define DATA 3
#define GOAL_LEFT 10
#define GOAL_RIGHT 9
#define LASER 11
#define LASER_IN_LEFT A3
#define LASER_IN_RIGHT A2

#define WIFI_RX 4
#define WIFI_TX 13

// I2C communication
#define I2C_CODE_MASTER 1
#define I2C_CODE_LEFT 2
#define I2C_CODE_RIGHT 3

// Settings
#define MAX_SCORE 10
#define GOAL_BLINK_TIME 500
#define CELEBRATION_TIME 2500
#define MIN_GOAL_DELAY 2000
#define GOAL_THRESHOLD1 0.8 // 0-1, higher number = more sensitive
#define GOAL_THRESHOLD2 0.8 // 0-1, higher number = more sensitive
#define GAME_END_TIME 3000 // grace period for last goal

#define PING_INTERVAL 30000
#define PING_TIMEOUT 5000

#endif // #ifndef _GOLEE_CONFIG_H_