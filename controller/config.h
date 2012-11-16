// Debug (0 or 1)
#define ERROR 1
#define LOG 2
#define INFO 3
#define DEBUG_LEVEL ERROR

// I2C communication
#define I2C_CODE_MASTER 1
#define I2C_CODE_LEFT 2
#define I2C_CODE_RIGHT 3

// RFID
#define RESET_ENABLED     0
#define RESET_TIME        1000
#define RFID_REMOVED_TIME 1500
#define RFID_TAG_LENGTH   5 // 5 Bytes
#define RFID_TAG_INPUT    12 // DATA (10 ASCII) + CHECK SUM (2 ASCII)

// Pin setup
#define RFID_RX  8
#define RFID_TX 9 // not used
#define LED 7
#define BUTTON_PLUS A3 // active low
#define BUTTON_MINUS A2 // active low
#define BUTTON_RESET 9 // active low
#define SPEAKER 10

// States
#define STATE_NONE 0
#define STATE_STARTED_UP 1
#define STATE_ENABLED 2

// Settings
#define LOADER_SPEED 100
#define BUTTON_DEBOUNCE_DELAY 10
#define BUTTON_HOLD_TIME 1600
#define BUTTON_MIN_TIME 100

// Misc
#define LOADER_FRAMES 8