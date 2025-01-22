#pragma once

#include "notifications/PlaybackControlNotification.h"

#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>

#include <Poco/Logger.h>
#include <Poco/NObserver.h>
//#include <Poco/LinearHashTable.h>

#include <Poco/Util/AbstractConfiguration.h>
#include <Poco/Util/Subsystem.h>
#include <memory>
#include <fstream>
#include <limits>
#include <iostream>
#include <sstream>
#include <unistd.h>

struct PresetItem{
        int rating{0};
        int playcount{0};
        std::string name;
};

struct DBPreset{
        int rating{0};
        int playcount{0};
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

    projectm_handle _projectM{nullptr}; //!< Pointer to the projectM instance used by the application.
    projectm_playlist_handle _playlist{nullptr}; //!< Pointer to the projectM playlist manager instance.

    Poco::NObserver<ProjectMWrapper, PlaybackControlNotification> _playbackControlNotificationObserver{*this, &ProjectMWrapper::PlaybackControlNotificationHandler};

    Poco::Logger& _logger{Poco::Logger::get("SDLRenderingWindow")}; //!< The class logger.
    
    typedef std::map<std::string, DBPreset> DBPM;
    DBPM dbpm;

    std::string _presetName;
    int _presetRating;
    int _presetPlaycount;
    
    FILE *filedb;
    char buffer[1024];
    
    bool put(int rating, int playcount, std::string name);

};
