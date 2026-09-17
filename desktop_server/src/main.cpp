#include <iostream>

#include "Config.h"
#include "InputSimulator.h"
#include "MotionProcessor.h"
#include "PacketHandler.h"
#include "UdpServer.h"

int main() {
    std::cout << "PhoneCursor desktop server\n";
    std::cout << "Listening on UDP port " << Config::kDefaultPort << "\n";
    std::cout << "Find this PC's local IP with 'ipconfig' and enter it in the "
                 "Android app (look for IPv4 Address under your Wi-Fi adapter).\n\n";

    InputSimulator inputSimulator;
    MotionProcessor motionProcessor;
    PacketHandler packetHandler(inputSimulator, motionProcessor);

    try {
        UdpServer server(Config::kDefaultPort);
        server.Run([&](const uint8_t* data, size_t length) {
            packetHandler.Handle(data, length);
        });
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
