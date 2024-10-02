#include "CoreEngine.h"
const Device CoreEngine::devices[] = {
    {"fan", "fan1"},
    {"room light", "led1"},
    {"kitchen light", "led2"},
    {"window", "window1"},
};

const Keywords CoreEngine::keys[] = {
    {"turn", ""},
    {"close", "close"},
    {"open", "open"},
};

CoreEngine::CoreEngine() : cloud_cmd("") {}

String CoreEngine::addCommasToCommand(String command) {
    String result = command;
    for (int i = 0; i < sizeof(keys) / sizeof(Keywords); i++) {
        String cmd_name = keys[i].cmd_name;
        int pos = result.indexOf(cmd_name);
        while (pos != -1) {
            if (pos > 0 && result[pos - 1] != ',') {
                result = result.substring(0, pos) + "," + result.substring(pos);
                pos++;
            }
            pos = result.indexOf(cmd_name, pos + cmd_name.length() + 1);
        }
    }
    return result;
}

void CoreEngine::executeCommand(String subCommand) {
    subCommand.trim();
    String cmd_name = "";
    String state = "";

    for (int i = 0; i < sizeof(keys) / sizeof(Keywords); i++) {
        int cmdPos = subCommand.indexOf(keys[i].cmd_name);
        if (cmdPos != -1) {
            cmd_name = keys[i].cmd_name;

            if (cmd_name == "turn") {
                if (subCommand.indexOf("on") != -1) {
                    state = "on";
                } else if (subCommand.indexOf("off") != -1) {
                    state = "off";
                }
            } else {
                state = keys[i].state;
            }

            subCommand = subCommand.substring(cmdPos + cmd_name.length());
            subCommand.trim();
            break;
        }
    }

    if (cmd_name != "" && state != "") {
        for (int j = 0; j < sizeof(devices) / sizeof(Device); j++) {
            if (subCommand.indexOf(devices[j].dev_name) != -1) {
                String sub_cloud = devices[j].kernel_name + state;
                cloud_cmd += sub_cloud + "~";  // Accumulate sub_cloud into cloud_cmd
            }
        }
    }
}

void CoreEngine::processCommand(String command) {
    while (command.length() > 0) {
        int cmdIndex = command.indexOf(',');
        String subCommand;
        
        if (cmdIndex == -1) {
            subCommand = command;
            command = "";
        } else {
            subCommand = command.substring(0, cmdIndex);
            command = command.substring(cmdIndex + 1);
        }

        subCommand.trim();
        executeCommand(subCommand);
    }
}

String CoreEngine::getCloudCmd() {
    return cloud_cmd;
}
