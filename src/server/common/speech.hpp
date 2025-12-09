#pragma once
#include "aip-cpp-sdk/speech.h"
#include "spdlog.hpp"

namespace MicroChat {

    class SpeechClient {
    public:
        SpeechClient(const std::string &app_id,
                     const std::string &api_key,
                     const std::string &secret_key)
            : client_(app_id, api_key, secret_key) {
            LOG_INFO("SpeechClient initialized with App ID: {}", app_id);
        }

        std::string recognize(const std::string &speech_data,
                              const std::string& err_msg,
                              const std::string &format = "pcm",
                              int sample_rate = 16000) {
            Json::Value result = client_.recognize(speech_data, format, sample_rate, aip::null);
            if (result["err_no"].asInt() != 0) {
                LOG_ERROR("Speech recognition error: {}", result["err_msg"].asString());
                err_msg = result["err_msg"].asString();
                return "";
            }

            std::string recognized_text = result["result"][0].asString();
            //LOG_INFO("Recognized text: {}", recognized_text);
            return recognized_text;
        }

    private:
        aip::Speech client_;
    };

} // namespace MicroChat