#pragma once

class BacklightAdapter;
class WiFiAdapter;

namespace rodakos {
class AudioFocusService;
class AudioOutputService;
class AudioService;
class FileService;
class MusicPlayerService;
class VoiceAssistantService;
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

    void SetAudioFocus(rodakos::AudioFocusService* audio_focus) { audio_focus_ = audio_focus; }
    rodakos::AudioFocusService* audio_focus() { return audio_focus_; }

    void SetVoiceAssistant(rodakos::VoiceAssistantService* voice_assistant) { voice_assistant_ = voice_assistant; }
    rodakos::VoiceAssistantService* voice_assistant() { return voice_assistant_; }

    void SetWebFiles(rodakos::WebFileSystemService* web_files) { web_files_ = web_files; }
    rodakos::WebFileSystemService* web_files() { return web_files_; }

private:
    BacklightAdapter* backlight_ = nullptr;
    WiFiAdapter* wifi_ = nullptr;
    rodakos::FileService* file_service_ = nullptr;
    rodakos::AudioService* audio_ = nullptr;
    rodakos::AudioOutputService* audio_output_ = nullptr;
    rodakos::MusicPlayerService* music_player_ = nullptr;
    rodakos::AudioFocusService* audio_focus_ = nullptr;
    rodakos::VoiceAssistantService* voice_assistant_ = nullptr;
    rodakos::WebFileSystemService* web_files_ = nullptr;
};
