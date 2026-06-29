#pragma once

class BacklightAdapter;
class WiFiAdapter;

namespace rodakos {
class AudioOutputService;
class AudioService;
class FileService;
class WebFileSystemService;
}

class PhoneServices {
public:
    void SetBacklight(BacklightAdapter* backlight) { backlight_ = backlight; }
    BacklightAdapter* backlight() { return backlight_; }

    void SetWiFi(WiFiAdapter* wifi) { wifi_ = wifi; }
    WiFiAdapter* wifi() { return wifi_; }

    void SetFileService(rodakos::FileService* file_service) { file_service_ = file_service; }
    rodakos::FileService* file_service() { return file_service_; }

    void SetAudio(rodakos::AudioService* audio) { audio_ = audio; }
    rodakos::AudioService* audio() { return audio_; }

    void SetAudioOutput(rodakos::AudioOutputService* audio_output) { audio_output_ = audio_output; }
    rodakos::AudioOutputService* audio_output() { return audio_output_; }

    void SetWebFiles(rodakos::WebFileSystemService* web_files) { web_files_ = web_files; }
    rodakos::WebFileSystemService* web_files() { return web_files_; }

private:
    BacklightAdapter* backlight_ = nullptr;
    WiFiAdapter* wifi_ = nullptr;
    rodakos::FileService* file_service_ = nullptr;
    rodakos::AudioService* audio_ = nullptr;
    rodakos::AudioOutputService* audio_output_ = nullptr;
    rodakos::WebFileSystemService* web_files_ = nullptr;
};
