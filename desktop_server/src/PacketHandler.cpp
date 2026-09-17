#include "PacketHandler.h"

#include "Protocol.h"

PacketHandler::PacketHandler(InputSimulator& inputSimulator, MotionProcessor& motionProcessor)
    : inputSimulator_(inputSimulator), motionProcessor_(motionProcessor) {}

void PacketHandler::Handle(const uint8_t* data, size_t length) {
    auto packet = Protocol::Parse(data, length);
    if (!packet) return;

    if (auto* sensor = std::get_if<Protocol::SensorPacket>(&*packet)) {
        motionProcessor_.OnSensor(*sensor, inputSimulator_);
    } else if (auto* click = std::get_if<Protocol::ClickPacket>(&*packet)) {
        inputSimulator_.Click(click->button);
    } else if (auto* key = std::get_if<Protocol::KeyPacket>(&*packet)) {
        if (key->action == Protocol::KeyAction::kChar) {
            inputSimulator_.TypeChar(key->character);
        } else {
            inputSimulator_.PressBackspace();
        }
    } else if (auto* calibrate = std::get_if<Protocol::CalibratePacket>(&*packet)) {
        motionProcessor_.Calibrate(*calibrate);
    }
    else if (auto* config = std::get_if<Protocol::ConfigPacket>(&*packet)) {
        uint8_t option = config->option;
        switch (option) {
            case (int)Protocol::ConfigOpt::yawMaxPercent: {
                motionProcessor_.setYawMaxPercent(config->data.f);
                return;
            }
            case (int)Protocol::ConfigOpt::pitchMaxPercent: {
                motionProcessor_.setPitchMaxPercent(config->data.f);
                return;
            }
            case (int)Protocol::ConfigOpt::yawInvert: {
                motionProcessor_.setInvertYaw(config->data.i == (uint8_t)1);
                return;
            }
            case (int)Protocol::ConfigOpt::pitchInvert: {
                motionProcessor_.setInvertPitch(config->data.i == (uint8_t)1);
                return;
            }
        }
    }
}
