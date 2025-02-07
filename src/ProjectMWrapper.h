#pragma once

#include "notifications/PlaybackControlNotification.h"

#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>

#include <Poco/Logger.h>
#include <Poco/NObserver.h>

#include <Poco/Util/AbstractConfiguration.h>
#include <Poco/Util/Subsystem.h>
#include <memory>
#include <fstream>
#include <limits>
#include <iostream>
#include <sstream>
#include <unistd.h>

#include "mpd/client.h"
#include "mpd/status.h"

struct DBPreset{
        int rating{0};
        int playcount{0};
};

struct MPDPlaylist{
    size_t id;
    std::string name;
};

enum CursorDir {
        cursordir_none = 0,
        cursordir_up = 1,
        cursordir_down,
        cursordir_pageup,
        cursordir_pagedown,
        cursordir_shift_up = 5,
        cursordir_shift_down,
        cursordir_shift_pageup,
        cursordir_shift_pagedown
};
    


class ProjectMWrapper : public Poco::Util::Subsystem
{
public:
    const char* name() const override;

    void initialize(Poco::Util::Application& app) override;

    void uninitialize() override;

    /**
     * Returns the projectM instance handle.
     * @return The projectM instance handle used to call API functions.
     */
    projectm_handle ProjectM() const;

    /**
     * Returns the playlist handle.
     * @return The plaslist handle.
     */
    projectm_playlist_handle Playlist() const;

    /**
     * Renders a single projectM frame.
     */
    void RenderFrame() const;

    /**
     * @brief Returns the targeted FPS value.
     * @return The user-configured target FPS. Can be 0, which means unlimited.
     */
    int TargetFPS();

    /**
     * @brief Updates projectM with the current, actual FPS value.
     * @param fps The current FPS value.
     */
    void UpdateRealFPS(float fps);

    /**
     * @brief If splash is disabled, shows the initial preset.
     * If shuffle is on, a random preset will be picked. Otherwise, the first playlist item is displayed.
     */
    void DisplayInitialPreset();

    /**
     * @brief Changes beat sensitivity by the given value.
     * @param value A positive or negative delta value.
     */
    void ChangeBeatSensitivity(float value);

    /**
     * @brief Returns the libprojectM version this application was built against.
     * @return A string with the libprojectM build version.
     */
    std::string ProjectMBuildVersion();

    /**
     * @brief Returns the libprojectM version this applications currently runs with.
     * @return A string with the libprojectM runtime library version.
     */
    std::string ProjectMRuntimeVersion();

    int PresetExists(std::string pName);

    int GetRating();
    int GetPlayCount();
    int GetPlayCount(std::string pName);


    void RatingUp();
    void RatingDown();
    void SetRating(int rating);
    void SetPlaycount(int playcount);
    void SetConfigPath(std::string path);

    void LoadDBPresets();


    struct mpd_connection* MPDGetConnection();
    void MPDSetRepeat(bool r);
    void MPDSetSingle(bool r);
    bool MPDGetRepeat();
    bool MPDGetSingle();
    void MPDGetStatus();
    void MPDVolumeUp();
    void MPDVolumeDown();
    void MPDNext();
    void MPDPrev();
    void MPDStop();
    void MPDPlay();
    void MPDPlayId (uint i);
    void MPDPlayPos(uint i);
    void MPDPause();
    void printErrorAndExit();

    std::string MPDGetSongName();
    std::string MPDGetSongInfo();
    uint MPDGetSongId(uint pos);

    
    std::string MPDQGet(uint i);
    bool MPDQDel(uint i);
    size_t      MPDQSize();
    std::string MPDPLGet(uint i);
    size_t      MPDPLSize();
    std::string MPDPVGet(uint i);
    size_t      MPDPVSize();
    
    void MPDListFiles();
    void MPDListFilesPreview(const char* name);
    void MPDListPlaylists();
    void MPDQueueAddPlaylist(const char* name, bool clear_queue = true);
    void MPDQueueAdd(const char* name);
    void MPDQueueDelete(uint id);
        
    void SetCursorDirUp(){cursor_dir = cursordir_up;}
    void SetCursorDirDown(){cursor_dir = cursordir_down;}
    void SetCursorDirPageUp(){cursor_dir =   cursordir_pageup;}
    void SetCursorDirPageDown(){cursor_dir = cursordir_pagedown;}
    void SetCursorDirNone(){cursor_dir = cursordir_none;}
    CursorDir GetCD(){ return cursor_dir; }
    void SetCD(CursorDir cdir){ cursor_dir = cdir; }
    int MPDQGetSongPos(){return _songPos;}
    
private:
    /**
     * @brief projectM callback. Called whenever a preset is switched.
     * @param isHardCut True if the switch was a hard cut.
     * @param index New preset playlist index.
     * @param context Callback context, e.g. "this" pointer.
     */
    static void PresetSwitchedEvent(bool isHardCut, unsigned int index, void* context);

    void PlaybackControlNotificationHandler(const Poco::AutoPtr<PlaybackControlNotification>& notification);

    std::vector<std::string> GetPathListWithDefault(const std::string& baseKey, const std::string& defaultPath);

    /**
     * @brief Event callback if a configuration value has changed.
     * @param property The key and value that has been changed.
     */
    void OnConfigurationPropertyChanged(const Poco::Util::AbstractConfiguration::KeyValue& property);

    /**
     * @brief Event callback if a configuration value has been removed.
     * @param key The key of the removed property.
     */
    void OnConfigurationPropertyRemoved(const std::string& key);

    Poco::AutoPtr<Poco::Util::AbstractConfiguration> _userConfig; //!< View of the "projectM" configuration subkey in the "user" configuration.
    Poco::AutoPtr<Poco::Util::AbstractConfiguration> _projectMConfigView; //!< View of the "projectM" configuration subkey in the "effective" configuration.
    Poco::AutoPtr<Poco::Util::AbstractConfiguration> _MPDConfigView; //!< View of the "projectM" configuration subkey in the "effective" configuration.

    projectm_handle _projectM{nullptr}; //!< Pointer to the projectM instance used by the application.
    projectm_playlist_handle _playlist{nullptr}; //!< Pointer to the projectM playlist manager instance.

    Poco::NObserver<ProjectMWrapper, PlaybackControlNotification> _playbackControlNotificationObserver{*this, &ProjectMWrapper::PlaybackControlNotificationHandler};

    Poco::Logger& _logger{Poco::Logger::get("SDLRenderingWindow")}; //!< The class logger.

    struct mpd_connection* mpdc;    

    typedef std::map<std::string, DBPreset> DBPM;
    DBPM dbpm;

    std::string configPath;
    std::string _presetName;
    int _presetRating;
    int _presetPlaycount;

    char infobuffer[1024];
    std::string _songName;
    std::string _songNameLast;
    std::string _songURI;
    std::string _songURILast;
    std::string _songInfo;
    std::vector<std::string> mpd_queue;
    std::vector<std::string> mpd_playlists;
    std::vector<std::string> mpd_preview;

    bool _mpdPlaying{false};
    size_t queue_size{0};
    size_t playlists_size{0};

    bool _mpd_repeat{false};
    bool _mpd_single{false};
    bool _mpd_queue_clear_add{true};
    int _mpd_volume{100};
    
    int _songPos{0};

    FILE *filedb;
    char buffer[1024];
    
    bool put(int rating, int playcount, std::string name);
    bool MPDConnect();
    CursorDir cursor_dir{cursordir_none};

};
