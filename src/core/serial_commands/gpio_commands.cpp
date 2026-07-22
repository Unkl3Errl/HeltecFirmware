#include "gpio_commands.h"
#include <globals.h>

bool is_free_gpio_pin(int pin_no) {
    // Only expose the two Heltec V4 Grove pins. The board definition aliases many
    // radio, OLED, GPS, battery, and control pins through the generic S3 target.
    std::vector<int> usable_pins = {GROVE_SDA, GROVE_SCL};

    for (int usable_pin : usable_pins)
        if (usable_pin >= 0 && pin_no == usable_pin) return true;

    return false;
}

uint32_t gpioModeCallback(cmd *c) {
    Command cmd(c);

    Argument arg = cmd.getArgument(0);
    String args = arg.getValue();
    args.trim();

    // const char* args = cmd_str.c_str() + strlen("gpio mode ");
    int pin_number = -1;
    int mode = 0;

    if (sscanf(args.c_str(), "%d %d", &pin_number, &mode) == 2) {
        // check usable pins according to the env
        if (mode >= 0 && mode <= 9 && is_free_gpio_pin(pin_number)) {
            pinMode(pin_number, mode);
            return true;
        }
    }

    serialDevice->print("Invalid args: ");
    serialDevice->println(args);
    return false;
}

uint32_t gpioSetCallback(cmd *c) {
    Command cmd(c);

    Argument arg = cmd.getArgument(0);
    String args = arg.getValue();
    args.trim();

    int pin_number = -1;
    int value = 0;

    if (sscanf(args.c_str(), "%d %d", &pin_number, &value) == 2) {
        // check usable pins according to the env
        if (value >= 0 && value <= 1 && is_free_gpio_pin(pin_number)) {
            digitalWrite(pin_number, value);
            return true;
        }
    }

    serialDevice->print("Invalid args: ");
    serialDevice->println(args);
    return false;
}

uint32_t gpioReadCallback(cmd *c) {
    Command cmd(c);

    Argument arg = cmd.getArgument(0);
    String args = arg.getValue();
    args.trim();

    int pin_number = -1;

    if (sscanf(args.c_str(), "%d", &pin_number) == 1) {
        // check usable pins according to the env
        if (is_free_gpio_pin(pin_number)) {
            serialDevice->println(digitalRead(pin_number));
            return true;
        }
    }

    serialDevice->print("Invalid args: ");
    serialDevice->println(args);
    return false;
}

void createGpioCommands(SimpleCLI *cli) {
    Command cmd = cli->addCompositeCmd("gpio");

    cmd.addSingleArgumentCommand("mode", gpioModeCallback);
    cmd.addSingleArgumentCommand("set", gpioSetCallback);
    cmd.addSingleArgumentCommand("read", gpioReadCallback);
}
