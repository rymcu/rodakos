#pragma once

class BacklightAdapter;
class WiFiAdapter;

namespace rodakos {
class AudioFocusService;
class AudioOutputService;
class AudioService;
class ButtonBindingService;
class CameraService;
class DeviceCloudConfigService;
class FileService;
class LightService;
class MotionService;
class MusicPlayerService;
class RecordingService;
class VoiceAssistantService;
class VoiceWakeService;
class WakeOnLanService;
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

    void SetMusicPlayer(rodakos::MusicPlayerService* music_player) { music_player_ = music_player; }
    rodakos::MusicPlayerService* music_player() { return music_player_; }

    void SetRecording(rodakos::RecordingService* recording) { recording_ = recording; }
    rodakos::RecordingService* recording() { return recording_; }

    void SetLights(rodakos::LightService* lights) { lights_ = lights; }
    rodakos::LightService* lights() { return lights_; }

    void SetMotion(rodakos::MotionService* motion) { motion_ = motion; }
    rodakos::MotionService* motion() { return motion_; }

    void SetButtons(rodakos::ButtonBindingService* buttons) { buttons_ = buttons; }
    rodakos::ButtonBindingService* buttons() { return buttons_; }

    void SetAudioFocus(rodakos::AudioFocusService* audio_focus) { audio_focus_ = audio_focus; }
    rodakos::AudioFocusService* audio_focus() { return audio_focus_; }

    void SetDeviceCloud(rodakos::DeviceCloudConfigService* device_cloud) { device_cloud_ = device_cloud; }
    rodakos::DeviceCloudConfigService* device_cloud() { return device_cloud_; }

    void SetVoiceAssistant(rodakos::VoiceAssistantService* voice_assistant) { voice_assistant_ = voice_assistant; }
    rodakos::VoiceAssistantService* voice_assistant() { return voice_assistant_; }

    void SetVoiceWake(rodakos::VoiceWakeService* voice_wake) { voice_wake_ = voice_wake; }
    rodakos::VoiceWakeService* voice_wake() { return voice_wake_; }

    void SetWakeOnLan(rodakos::WakeOnLanService* wake_on_lan) { wake_on_lan_ = wake_on_lan; }
    rodakos::WakeOnLanService* wake_on_lan() { return wake_on_lan_; }

    void SetWebFiles(rodakos::WebFileSystemService* web_files) { web_files_ = web_files; }
    rodakos::WebFileSystemService* web_files() { return web_files_; }

    void SetCamera(rodakos::CameraService* camera) { camera_ = camera; }
    rodakos::CameraService* camera() { return camera_; }

private:
    BacklightAdapter* backlight_ = nullptr;
    WiFiAdapter* wifi_ = nullptr;
    rodakos::FileService* file_service_ = nullptr;
    rodakos::AudioService* audio_ = nullptr;
    rodakos::AudioOutputService* audio_output_ = nullptr;
    rodakos::MusicPlayerService* music_player_ = nullptr;
    rodakos::RecordingService* recording_ = nullptr;
    rodakos::LightService* lights_ = nullptr;
    rodakos::MotionService* motion_ = nullptr;
    rodakos::ButtonBindingService* buttons_ = nullptr;
    rodakos::AudioFocusService* audio_focus_ = nullptr;
    rodakos::DeviceCloudConfigService* device_cloud_ = nullptr;
    rodakos::VoiceAssistantService* voice_assistant_ = nullptr;
    rodakos::VoiceWakeService* voice_wake_ = nullptr;
    rodakos::WakeOnLanService* wake_on_lan_ = nullptr;
    rodakos::WebFileSystemService* web_files_ = nullptr;
    rodakos::CameraService* camera_ = nullptr;
};
