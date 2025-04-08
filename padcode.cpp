#include <TM1637Display.h>
#include <IRremote.h>

// Define the pins for the displays
#define CLK_MAIN 2
#define DIO_MAIN 3

// Define the pins for the LEDs
#define LED1 6         // Green LED - Connection OK
#define LED2 7         // Red LED - Connection Error
#define KEY_SWITCH 8   // Key switch input
#define ENABLE_SWITCH 11 // Enable switch for launch systems
#define LAUNCH_BUTTON 10 // Launch button
#define EMERGENCY_STOP 15 // Emergency stop button
#define IR_SEND_PIN 9     // IR Send Pin
#define IR_RECEIVE_PIN 14  // IR Receive Pin

// Define the pins for the launch switches
#define LAUNCH_SWITCH_1 12
#define LAUNCH_SWITCH_2 13
#define LAUNCH_SWITCH_3 A0 // Analog pins can also be used as digital
#define LAUNCH_SWITCH_4 A1
#define LAUNCH_SWITCH_5 A2

// Define the number of displays and launch systems
#define NUM_DISPLAYS 6
#define NUM_LAUNCH_SYSTEMS 5
#define MAIN_DISPLAY_INDEX 0

// Define the display objects
TM1637Display mainDisplay(CLK_MAIN, DIO_MAIN);
TM1637Display lpdDisplays[NUM_DISPLAYS - 1] = {
    TM1637Display(CLK_LPD, DIO_LPD),
    TM1637Display(CLK_LPD, DIO_LPD),
    TM1637Display(CLK_LPD, DIO_LPD),
    TM1637Display(CLK_LPD, DIO_LPD),
    TM1637Display(CLK_LPD, DIO_LPD)};

// Define the IR remote object
IRsend irsend(IR_SEND_PIN);
IRrecv irrecv(IR_RECEIVE_PIN);
decode_results results;

// Define the IR messages
#define IR_CONNECTION_REQUEST 0x12345678 // Initial connection request
#define IR_CONNECTION_REPLY 0x87654321   // Reply from pad computer
#define IR_LAUNCH_MESSAGE 0x789ABCDE     // Launch command

// Define constants for timeouts and retries
#define CONNECTION_TIMEOUT 5000    // 5 seconds
#define SWITCH_DEBOUNCE_DELAY 50  // 50 milliseconds for switch debouncing

// Define variables for the key switch, enable switch, and system state
int keySwitchState = HIGH;
int enableSwitchState = HIGH;
bool systemOn = false;
bool launchEnabled = false;
bool connectionEstablished = false;
int launchButtonState = HIGH; // Initialize as HIGH because of pull-up
int lastLaunchButtonState = HIGH;
int emergencyStopState = HIGH; // Initialize as HIGH because of pull-up
int lastEmergencyStopState = HIGH;
bool systemStopped = false;

// Array to store the state of the launch switches
int launchSwitchStates[NUM_LAUNCH_SYSTEMS];
// Array to store the pins of the launch switches
int launchSwitchPins[NUM_LAUNCH_SYSTEMS] = {
    LAUNCH_SWITCH_1,
    LAUNCH_SWITCH_2,
    LAUNCH_SWITCH_3,
    LAUNCH_SWITCH_4,
    LAUNCH_SWITCH_5};

// Global variables for the launch sequence
bool launchSequenceRunning = false;
int currentPad = 0;
unsigned long mainCountdownStartTime = 0;
unsigned long padTimerStartTime[NUM_LAUNCH_SYSTEMS]; // Array to store start times for each pad
int padSecondsElapsed[NUM_LAUNCH_SYSTEMS];
bool padTimerFrozen[NUM_LAUNCH_SYSTEMS];       // Array to indicate if a pad timer is frozen
const int mainCountdownDuration = 5000;             // 5 seconds for main countdown
const int padTimerDuration = 1000;                // 1 second per pad timer tick
unsigned long lastDebounceTime[6]; // For debouncing switches.  Need 6: 1 x KEY, 1 x ENABLE, 1 x LAUNCH, 1 x EMERGENCY STOP and 2 spare.

// Function declarations
void startupAnimation();
void establishConnection();
void handleIRCommunication();
void readLaunchSwitches();
void startLaunchSequence();
void runLaunchSequence();
void clearDisplays();
bool debounceSwitch(uint8_t pin, int &lastState, unsigned long &lastDebounceTime);

void setup() {
    // Initialize the serial port
    Serial.begin(9600);

    // Initialize the displays
    mainDisplay.setBrightness(7);
    for (int i = 0; i < NUM_DISPLAYS - 1; i++) {
        lpdDisplays[i].setBrightness(7);
    }

    // Initialize the LED pins
    pinMode(LED1, OUTPUT);
    pinMode(LED2, OUTPUT);
    digitalWrite(LED1, LOW);
    digitalWrite(LED2, LOW);

    // Initialize the key switch, enable switch, launch button, and emergency stop pins
    pinMode(KEY_SWITCH, INPUT_PULLUP);
    pinMode(ENABLE_SWITCH, INPUT_PULLUP);
    pinMode(LAUNCH_BUTTON, INPUT_PULLUP); // Use pull-up for the launch button
    pinMode(EMERGENCY_STOP, INPUT_PULLUP); // Use pull-up for the emergency stop button

    // Initialize the launch switch pins
    for (int i = 0; i < NUM_LAUNCH_SYSTEMS; i++) {
        pinMode(launchSwitchPins[i], INPUT_PULLUP);
        padTimerStartTime[i] = 0;          // Initialize pad timer start times
        padSecondsElapsed[i] = 0;
        padTimerFrozen[i] = false;         // Initialize pad timers as not frozen
    }

    // Initialize the IR receiver
    irrecv.enableIRIn();

    // Initialize lastDebounceTime
    lastDebounceTime[0] = 0; // KEY_SWITCH
    lastDebounceTime[1] = 0; // ENABLE_SWITCH
    lastDebounceTime[2] = 0; // LAUNCH_BUTTON
    lastDebounceTime[3] = 0; // EMERGENCY_STOP
    lastDebounceTime[4] = 0; // Spare
    lastDebounceTime[5] = 0; // Spare
}

void loop() {
    // Read the emergency stop button state
    if (debounceSwitch(EMERGENCY_STOP, emergencyStopState, lastDebounceTime[3])) {
        if (emergencyStopState == LOW) { // Was just pressed
            systemStopped = !systemStopped; // Toggle the system stopped state
            if (systemStopped) {
                Serial.println("Emergency Stop Activated!");
                digitalWrite(LED1, LOW);
                digitalWrite(LED2, LOW);
                clearDisplays();
                launchSequenceRunning = false; // Stop any running sequence
            } else {
                Serial.println("Emergency Stop Released!");
                // Do not start the launch sequence here.  The key switch must be cycled.
            }
        }
    }

    if (systemStopped) {
        // Stop all operations
        return; // Exit the loop and do nothing else until reset
    }

    // Read the key switch state
    if (debounceSwitch(KEY_SWITCH, keySwitchState, lastDebounceTime[0])) {
        if (keySwitchState == LOW) {
            if (!systemOn) {
                systemOn = true;
                startupAnimation();
                establishConnection();
            }
        } else {
            // If the key switch is off, turn off everything
            systemOn = false;
            launchEnabled = false;
            connectionEstablished = false;
            launchSequenceRunning = false; // Stop any running sequence
            digitalWrite(LED1, LOW);
            digitalWrite(LED2, LOW);
            clearDisplays();
        }
    }

    // Read the enable switch state
    if (debounceSwitch(ENABLE_SWITCH, enableSwitchState, lastDebounceTime[1])) {
        launchEnabled = (enableSwitchState == LOW); // Update launchEnabled based on switch
    }

    // Read the launch switch states
    if (systemOn) {
        readLaunchSwitches();
    }

    // Read the launch button state
    if (debounceSwitch(LAUNCH_BUTTON, launchButtonState, lastDebounceTime[2])) {
        if (launchEnabled && launchButtonState == LOW) {
            // Launch button pressed, start the sequence if not already running
            if (!launchSequenceRunning) {
                startLaunchSequence();
            }
        }
    }

    // Handle the launch sequence if it's running
    if (launchSequenceRunning) {
        runLaunchSequence();
    }

    // Handle IR communication
    if (systemOn) {
        handleIRCommunication();
    }
}

void startupAnimation() {
    // Array of patterns for the animation
    const uint8_t patterns[] = {
        0b00111111, // 0
        0b00000110, // 1
        0b01011011, // 2
        0b01001111, // 3
        0b01100110, // 4
        0b01101101, // 5
        0b01111101, // 6
        0b00000111, // 7
        0b01111111, // 8
        0b01101111  // 9
    };
    const int animationDuration = 1000;
    const int frameDuration = animationDuration / 10;
    unsigned long startTime = millis();

    for (int i = 0; i < 10; i++) {
        mainDisplay.setSegments(&patterns[i], 1, 0);
        for (int j = 0; j < NUM_DISPLAYS - 1; j++) {
            lpdDisplays[j].setSegments(&patterns[i], 1, 0);
        }
        delay(frameDuration);
    }
    clearDisplays();
}

void establishConnection() {
    unsigned long startTime = millis();
    connectionEstablished = false; // Reset
    digitalWrite(LED2, HIGH); // Start with error LED on

    while (!connectionEstablished && systemOn && (millis() - startTime < CONNECTION_TIMEOUT)) {
        irsend.sendRaw((unsigned long *)&IR_CONNECTION_REQUEST, 32, 38);
        Serial.println("Connection request sent");
        delay(100); // Short delay for IR send

        if (irrecv.decode(&results)) {
            Serial.println("Received something");
            if (results.value == IR_CONNECTION_REPLY) {
                connectionEstablished = true;
                digitalWrite(LED1, HIGH);
                digitalWrite(LED2, LOW);
                Serial.println("Connection established!");
            }
            irrecv.resume();
        }
    }

    if (!connectionEstablished) {
        Serial.println("Connection failed!");
        digitalWrite(LED2, HIGH);
        digitalWrite(LED1, LOW);
    }
}

void handleIRCommunication() {
    receiveIRMessage();
}

void receiveIRMessage() {
  if (irrecv.decode(&results)) {
    Serial.println("Received IR data");
    if (results.value == IR_CONNECTION_REPLY) {
      //handle connection
    }
    irrecv.resume();
  }
}

void readLaunchSwitches() {
    for (int i = 0; i < NUM_LAUNCH_SYSTEMS; i++) {
        launchSwitchStates[i] = digitalRead(launchSwitchPins[i]);
    }
}

void startLaunchSequence() {
    launchSequenceRunning = true;
    currentPad = 0;
    mainCountdownStartTime = 0; // Reset for a fresh countdown
    for (int i = 0; i < NUM_LAUNCH_SYSTEMS; i++) {
        padTimerStartTime[i] = 0;  // Reset pad timers
        padSecondsElapsed[i] = 0;
        padTimerFrozen[i] = false; // Ensure timers are not frozen at the start
    }
    Serial.println("Launch sequence started!");
}

void runLaunchSequence() {
    if (currentPad < NUM_LAUNCH_SYSTEMS) {
        // Check if the current pad's switch is on
        if (launchSwitchStates[currentPad] == LOW) {
            if (mainCountdownStartTime == 0) {
                mainCountdownStartTime = millis(); // Start the countdown
            }

            // Calculate remaining time for main countdown
            long timeRemaining = mainCountdownDuration - (millis() - mainCountdownStartTime);
            if (timeRemaining > 0) {
                // Display countdown on main display
                int secondsRemaining = timeRemaining / 1000;
                int displayValue = (secondsRemaining * 100) + (secondsRemaining); // Display as T-0X
                mainDisplay.display(displayValue);
                Serial.print("Main countdown: T-");
                Serial.println(secondsRemaining);

                // Pad timer display
                if (padTimerStartTime[currentPad] == 0) {
                    padTimerStartTime[currentPad] = millis();
                }
                if (!padTimerFrozen[currentPad]) { // Only update if not frozen
                    long padTimeElapsed = millis() - padTimerStartTime[currentPad];
                    padSecondsElapsed[currentPad] = padTimeElapsed / 1000;
                }
                int padDisplayValue = (padSecondsElapsed[currentPad] / 10) % 10 * 10 + padSecondsElapsed[currentPad] % 10;
                lpdDisplays[currentPad].display(padDisplayValue);
                Serial.print("Pad ");
                Serial.print(currentPad + 1);
                Serial.print(" Timer: T0:0");
                Serial.println(padSecondsElapsed[currentPad]);
            } else {
                // Main countdown is finished for this pad
                mainDisplay.display(0);                                          // Clear main display
                lpdDisplays[currentPad].display(0); // Clear pad display
                Serial.print("Launch command sent for pad ");
                Serial.println(currentPad + 1);
                irsend.sendRaw((unsigned long *)&IR_LAUNCH_MESSAGE, 32, 38); // Send launch command
                delay(100);
                irrecv.resume();
                currentPad++;         // Move to the next pad
                mainCountdownStartTime = 0; // Reset for next pad
            }
        } else {
            //if switch is off, freeze timer.
            padTimerFrozen[currentPad] = true;
            currentPad++;         // Move to the next pad
            mainCountdownStartTime = 0; // Reset for next pad
        }
    } else {
        // Launch sequence is complete
        launchSequenceRunning = false;
        Serial.println("Launch sequence complete!");
        clearDisplays();
    }
}

void clearDisplays() {
    int emptyDisplay[] = {0, 0, 0, 0};
    mainDisplay.setDigits(emptyDisplay);
    for (int i = 0; i < NUM_DISPLAYS - 1; i++) {
        lpdDisplays[i].setDigits(emptyDisplay);
    }
}

bool debounceSwitch(uint8_t pin, int &lastState, unsigned long &lastDebounceTime) {
    int reading = digitalRead(pin);
    unsigned long currentMillis = millis();

    if (reading != lastState) {
        lastDebounceTime = currentMillis;
    }

    if (currentMillis - lastDebounceTime >= SWITCH_DEBOUNCE_DELAY) {
        if (reading != lastState) {
            lastState = reading;
            return true; //state changed
        }
    }
    return false; //state did not change
}
