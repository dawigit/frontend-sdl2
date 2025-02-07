#include "ProjectMWrapper.h"

#include "ProjectMSDLApplication.h"
#include "SDLRenderingWindow.h"

#include "notifications/DisplayToastNotification.h"

#include <Poco/Delegate.h>
#include <Poco/File.h>
#include <Poco/NotificationCenter.h>

#include <SDL2/SDL_opengl.h>

#include <cmath>
#include <map>

#include <assert.h>

inline bool ProjectMWrapper::put(int rating, int playcount, std::string name)
{
    sprintf(buffer, "%d %d %s\n", rating, playcount, name.c_str() );
    size_t i = fwrite (buffer , strlen(buffer), sizeof(char), filedb);
    //printf("[%ld] %ld buffer: %s", i, strlen(buffer), buffer);
    return true;
}

const char* ProjectMWrapper::name() const
{
    return "ProjectM Wrapper";
}

void ProjectMWrapper::initialize(Poco::Util::Application& app)
{
    auto& projectMSDLApp = dynamic_cast<ProjectMSDLApplication&>(app);
    _projectMConfigView = projectMSDLApp.config().createView("projectM");
    _MPDConfigView = projectMSDLApp.config().createView("MPD");
    _userConfig = projectMSDLApp.UserConfiguration();
    poco_information_f1(_logger, "Events enabled: %?d", _projectMConfigView->eventsEnabled());


    if (!_projectM)
    {
        auto& sdlWindow = app.getSubsystem<SDLRenderingWindow>();

        int canvasWidth{0};
        int canvasHeight{0};

        sdlWindow.GetDrawableSize(canvasWidth, canvasHeight);

        auto presetPaths = GetPathListWithDefault("presetPath", app.config().getString("application.dir", ""));
        auto texturePaths = GetPathListWithDefault("texturePath", app.config().getString("", ""));

        _projectM = projectm_create();
        if (!_projectM)
        {
            poco_error(_logger, "Failed to initialize projectM. Possible reasons are a lack of required OpenGL features or GPU resources.");
            throw std::runtime_error("projectM initialization failed");
        }

        int fps = _projectMConfigView->getInt("fps", 60);
        if (fps <= 0)
        {
            // We don't know the target framerate, pass in a default of 60.
            fps = 60;
        }

        projectm_set_window_size(_projectM, canvasWidth, canvasHeight);
        projectm_set_fps(_projectM, fps);
        projectm_set_mesh_size(_projectM, _projectMConfigView->getInt("meshX", 48), _projectMConfigView->getInt("meshY", 32));
        projectm_set_aspect_correction(_projectM, _projectMConfigView->getBool("aspectCorrectionEnabled", true));
        projectm_set_preset_locked(_projectM, _projectMConfigView->getBool("presetLocked", false));

        // Preset display settings
        projectm_set_preset_duration(_projectM, _projectMConfigView->getDouble("displayDuration", 30.0));
        projectm_set_soft_cut_duration(_projectM, _projectMConfigView->getDouble("transitionDuration", 3.0));
        projectm_set_hard_cut_enabled(_projectM, _projectMConfigView->getBool("hardCutsEnabled", false));
        projectm_set_hard_cut_duration(_projectM, _projectMConfigView->getDouble("hardCutDuration", 20.0));
        projectm_set_hard_cut_sensitivity(_projectM, static_cast<float>(_projectMConfigView->getDouble("hardCutSensitivity", 1.0)));
        projectm_set_beat_sensitivity(_projectM, static_cast<float>(_projectMConfigView->getDouble("beatSensitivity", 1.0)));

        if (!texturePaths.empty())
        {
            std::vector<const char*> texturePathList;
            texturePathList.reserve(texturePaths.size());
            for (const auto& texturePath : texturePaths)
            {
                texturePathList.push_back(texturePath.data());
            }

            projectm_set_texture_search_paths(_projectM, texturePathList.data(), texturePaths.size());
        }

        // Playlist
        _playlist = projectm_playlist_create(_projectM);
        if (!_playlist)
        {

            poco_error(_logger, "Failed to create the projectM preset playlist manager instance.");
            throw std::runtime_error("Playlist initialization failed");
        }

        projectm_playlist_set_shuffle(_playlist, _projectMConfigView->getBool("shuffleEnabled", true));
        
        for (const auto& presetPath : presetPaths)
        {
            Poco::File file(presetPath);
            if (file.exists() && file.isFile())
            {
                projectm_playlist_add_preset(_playlist, presetPath.c_str(), false);
            }
            else
            {
                // Symbolic links also fall under this. Without complex resolving, we can't
                // be sure what the link exactly points to, especially if a trailing slash is missing.
                projectm_playlist_add_path(_playlist, presetPath.c_str(), true, false);
            }
        }
        projectm_playlist_sort(_playlist, 0, projectm_playlist_size(_playlist), SORT_PREDICATE_FILENAME_ONLY, SORT_ORDER_ASCENDING);

        projectm_playlist_set_preset_switched_event_callback(_playlist, &ProjectMWrapper::PresetSwitchedEvent, static_cast<void*>(this));

    }

    Poco::NotificationCenter::defaultCenter().addObserver(_playbackControlNotificationObserver);

    // Observe user configuration changes (set via the settings window)
    _userConfig->propertyChanged += Poco::delegate(this, &ProjectMWrapper::OnConfigurationPropertyChanged);
    _userConfig->propertyRemoved += Poco::delegate(this, &ProjectMWrapper::OnConfigurationPropertyRemoved);

    struct mpd_song *song;

//    std::string mpdc_host = app.config().getString("mpd.host");
//    int mpdc_port = 6600;
//    mpdc_port = app.config().getInt("mpd.port");
//    printf("MPD host: '%s' MPD port: %d\n",mpdc_host.c_str(),mpdc_port);
    
    if(app.config().getString("mpd.host").size()>0){
        MPDConnect();
    }

}

void ProjectMWrapper::uninitialize()
{
    _userConfig->propertyRemoved -= Poco::delegate(this, &ProjectMWrapper::OnConfigurationPropertyRemoved);
    _userConfig->propertyChanged -= Poco::delegate(this, &ProjectMWrapper::OnConfigurationPropertyChanged);
    Poco::NotificationCenter::defaultCenter().removeObserver(_playbackControlNotificationObserver);
    
    if(filedb){
        fclose(filedb);
        filedb=nullptr;
    }

    filedb = fopen(std::string(configPath+"dbpresets").c_str(), "w");
        
    size_t pli = 0;
    char* plitem=NULL;
    while(true){
        plitem  =  projectm_playlist_item(_playlist, pli);
        if(plitem){
            if(dbpm.count(plitem)>0 && dbpm[plitem].playcount>0){
                put(dbpm[plitem].rating, dbpm[plitem].playcount, std::string(plitem) );
                free(plitem);
            }
            ++pli;
        }else{
            printf("last\n");
            break;
        }
    }


    if (_projectM)
    {
        projectm_destroy(_projectM);
        _projectM = nullptr;
    }

    if (_playlist)
    {
        projectm_playlist_destroy(_playlist);
        _playlist = nullptr;
    }
    
    if(filedb)fclose(filedb);filedb=nullptr;


}

projectm_handle ProjectMWrapper::ProjectM() const
{
    return _projectM;
}

projectm_playlist_handle ProjectMWrapper::Playlist() const
{
    return _playlist;
}

int ProjectMWrapper::TargetFPS()
{
    return _projectMConfigView->getInt("fps", 60);
}

void ProjectMWrapper::UpdateRealFPS(float fps)
{
    projectm_set_fps(_projectM, static_cast<uint32_t>(std::round(fps)));
}

void ProjectMWrapper::RenderFrame() const
{
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    size_t currentMeshX{0};
    size_t currentMeshY{0};
    projectm_get_mesh_size(_projectM, &currentMeshX, &currentMeshY);
    if (currentMeshX != _projectMConfigView->getInt("meshX", 220) ||
        currentMeshY != _projectMConfigView->getInt("meshY", 125))
    {
        projectm_set_mesh_size(_projectM, _projectMConfigView->getInt("meshX", 220), _projectMConfigView->getInt("meshY", 125));
    }

    projectm_opengl_render_frame(_projectM);
}

void ProjectMWrapper::DisplayInitialPreset()
{
    if (!_projectMConfigView->getBool("enableSplash", true))
    {
        if (_projectMConfigView->getBool("shuffleEnabled", true))
        {
            projectm_playlist_play_next(_playlist, true);
        }
        else
        {
            projectm_playlist_set_position(_playlist, 0, true);
        }
    }
}

void ProjectMWrapper::ChangeBeatSensitivity(float value)
{
    projectm_set_beat_sensitivity(_projectM, projectm_get_beat_sensitivity(_projectM) + value);
    Poco::NotificationCenter::defaultCenter().postNotification(
        new DisplayToastNotification(Poco::format("Beat Sensitivity: %.2hf", projectm_get_beat_sensitivity(_projectM))));
}

std::string ProjectMWrapper::ProjectMBuildVersion()
{
    return PROJECTM_VERSION_STRING;
}

std::string ProjectMWrapper::ProjectMRuntimeVersion()
{
    auto* projectMVersion = projectm_get_version_string();
    std::string projectMRuntimeVersion(projectMVersion);
    projectm_free_string(projectMVersion);

    return projectMRuntimeVersion;
}

int ProjectMWrapper::PresetExists(std::string pName){
    int c = dbpm.count(pName);
    return c;
}

void ProjectMWrapper::PresetSwitchedEvent(bool isHardCut, unsigned int index, void* context)
{
    auto that = reinterpret_cast<ProjectMWrapper*>(context);
    auto presetName = projectm_playlist_item(that->_playlist, index);
    std::string pname(presetName);
    
    that->_presetName = pname;
    int pc = 0;
    size_t c = that->dbpm.count(pname);
    int r = projectm_get_preset_rating(that->_projectM);
    //printf("dbmp:[%ld] (%d) '%s'\n", c, r, pname.c_str());
    if(c>0){
        pc = that->dbpm[pname].playcount;
        r = that->dbpm[pname].rating;
    }else{
        that->dbpm.insert(std::pair<std::string,DBPreset>(pname,DBPreset{r,pc}));        
        that->dbpm[pname].rating = r;
    }
    ++that->dbpm[pname].playcount;  // = that->dbpm[pname].playcount +1;
    that->_presetRating = that->dbpm[pname].rating;
    that->_presetPlaycount = that->dbpm[pname].playcount;

    poco_information_f1(that->_logger, "Displaying preset: %s", std::string(presetName));
    Poco::NotificationCenter::defaultCenter().postNotification(
        new DisplayToastNotification(Poco::format("%s", std::string(presetName))));
    projectm_playlist_free_string(presetName);

    Poco::NotificationCenter::defaultCenter().postNotification(new UpdateWindowTitleNotification);
}

void ProjectMWrapper::PlaybackControlNotificationHandler(const Poco::AutoPtr<PlaybackControlNotification>& notification)
{
    switch (notification->ControlAction())
    {
        case PlaybackControlNotification::Action::NextPreset:
            projectm_playlist_play_next(_playlist, !notification->SmoothTransition());
            break;

        case PlaybackControlNotification::Action::PreviousPreset:
            projectm_playlist_play_previous(_playlist, !notification->SmoothTransition());
            break;

        case PlaybackControlNotification::Action::LastPreset:
            projectm_playlist_play_last(_playlist, !notification->SmoothTransition());
            break;

        case PlaybackControlNotification::Action::RandomPreset: {
            bool shuffleEnabled = projectm_playlist_get_shuffle(_playlist);
            projectm_playlist_set_shuffle(_playlist, true);
            projectm_playlist_play_next(_playlist, !notification->SmoothTransition());
            projectm_playlist_set_shuffle(_playlist, shuffleEnabled);
            break;
        }

        case PlaybackControlNotification::Action::ToggleShuffle:
            _userConfig->setBool("projectM.shuffleEnabled", !projectm_playlist_get_shuffle(_playlist));
            break;

        case PlaybackControlNotification::Action::TogglePresetLocked: {
            _userConfig->setBool("projectM.presetLocked", !projectm_get_preset_locked(_projectM));
            break;
        }
    }
}

std::vector<std::string> ProjectMWrapper::GetPathListWithDefault(const std::string& baseKey, const std::string& defaultPath)
{
    using Poco::Util::AbstractConfiguration;

    std::vector<std::string> pathList;
    auto defaultPresetPath = _projectMConfigView->getString(baseKey, defaultPath);
    if (!defaultPresetPath.empty())
    {
        pathList.push_back(defaultPresetPath);
    }
    AbstractConfiguration::Keys subKeys;
    _projectMConfigView->keys(baseKey, subKeys);
    for (const auto& key : subKeys)
    {
        auto path = _projectMConfigView->getString(baseKey + "." + key, "");
        if (!path.empty())
        {
            pathList.push_back(std::move(path));
        }
    }
    return pathList;
}

void ProjectMWrapper::OnConfigurationPropertyChanged(const Poco::Util::AbstractConfiguration::KeyValue& property)
{
    OnConfigurationPropertyRemoved(property.key());
}

void ProjectMWrapper::OnConfigurationPropertyRemoved(const std::string& key)
{
    if (_projectM == nullptr || _playlist == nullptr)
    {
        return;
    }

    if (key == "projectM.presetLocked")
    {
        projectm_set_preset_locked(_projectM, _projectMConfigView->getBool("presetLocked", false));
        Poco::NotificationCenter::defaultCenter().postNotification(new UpdateWindowTitleNotification);
    }

    if (key == "projectM.shuffleEnabled")
    {
        projectm_playlist_set_shuffle(_playlist, _projectMConfigView->getBool("shuffleEnabled", true));
    }

    if (key == "projectM.aspectCorrectionEnabled")
    {
        projectm_set_aspect_correction(_projectM, _projectMConfigView->getBool("aspectCorrectionEnabled", true));
    }

    if (key == "projectM.displayDuration")
    {
        projectm_set_preset_duration(_projectM, _projectMConfigView->getDouble("displayDuration", 30.0));
    }

    if (key == "projectM.transitionDuration")
    {
        projectm_set_soft_cut_duration(_projectM, _projectMConfigView->getDouble("transitionDuration", 3.0));
    }

    if (key == "projectM.hardCutsEnabled")
    {
        projectm_set_aspect_correction(_projectM, _projectMConfigView->getBool("hardCutsEnabled", false));
    }

    if (key == "projectM.hardCutDuration")
    {
        projectm_set_hard_cut_duration(_projectM, _projectMConfigView->getDouble("hardCutDuration", 20.0));
    }

    if (key == "projectM.hardCutSensitivity")
    {
        projectm_set_hard_cut_sensitivity(_projectM, static_cast<float>(_projectMConfigView->getDouble("hardCutSensitivity", 1.0)));
    }

    if (key == "projectM.meshX" || key == "projectM.meshY")
    {
        projectm_set_mesh_size(_projectM, _projectMConfigView->getUInt64("meshX", 48), _projectMConfigView->getUInt64("meshY", 32));
    }
}

int ProjectMWrapper::GetRating(){
    if(!dbpm.count(_presetName))return projectm_get_preset_rating(_projectM);
    return dbpm[_presetName].rating;
}

int ProjectMWrapper::GetPlayCount(){
    return dbpm[_presetName].playcount;
}

int ProjectMWrapper::GetPlayCount(std::string pName){
    return dbpm[pName].playcount;
}

void ProjectMWrapper::RatingDown(){
    if( dbpm[_presetName].rating > 0 ) dbpm[_presetName].rating--;
}

void ProjectMWrapper::RatingUp(){
    if( dbpm[_presetName].rating < 5 ) dbpm[_presetName].rating++;
}

void ProjectMWrapper::SetRating(int rating){
    if(rating <0 || rating >5)return;
    dbpm[_presetName].rating = rating;
}

void ProjectMWrapper::SetPlaycount(int playcount){
    dbpm[_presetName].playcount = playcount;
}

void ProjectMWrapper::SetConfigPath(std::string path){
    configPath = path;
}

void ProjectMWrapper::LoadDBPresets(){

    filedb = fopen(std::string(configPath+"dbpresets").c_str(), "r+");

    if(filedb){
        char* line = NULL;
        size_t len = 0;
        size_t pli = 0;
        int rating, playcount;
        ssize_t nread;
        std::string name;
        while ((nread = getline(&line, &len, filedb)) != -1){
            std::stringstream is(line);
            is << line;
            is >> rating;
            is >> playcount;
            name = std::string(line).substr(2);
            name = name.substr(name.find_first_of(" ")+1);
            name = name.substr(0,name.size()-1);
            //printf("<- %d %d '%s'\n",rating, playcount, name.c_str());
            dbpm.insert(std::pair<std::string,DBPreset>(name,DBPreset{rating,playcount}));
            //printf("* %d %d '%s'\n",dbpm[name].rating, dbpm[name].playcount, name.c_str());
        }
        if(line)free(line);
        fclose(filedb);
        filedb=nullptr;
    }

}

inline void ProjectMWrapper::printErrorAndExit()
{
    assert(mpd_connection_get_error(mpdc) == MPD_ERROR_SUCCESS);

    const char *message = mpd_connection_get_error_message(mpdc);
    if (mpd_connection_get_error(mpdc) == MPD_ERROR_SERVER)
        fprintf(stderr, "MPD error: %s\n", message);
    mpd_connection_free(mpdc);
    exit(EXIT_FAILURE);
}


void ProjectMWrapper::MPDSetRepeat(bool r){ mpd_run_repeat(mpdc, r); }
void ProjectMWrapper::MPDSetSingle(bool r){ mpd_run_single(mpdc, r); }

bool ProjectMWrapper::MPDGetRepeat(){ return _mpd_repeat; }
bool ProjectMWrapper::MPDGetSingle(){ return _mpd_single; }

void ProjectMWrapper::MPDVolumeUp()
{ 
    if(_mpd_volume<100){
        ++_mpd_volume;
        mpd_run_set_volume(mpdc, _mpd_volume);
        Poco::NotificationCenter::defaultCenter().postNotification(
            new DisplayToastNotification(Poco::format("MPD Volume Up: %3d", _mpd_volume)));
            poco_information_f1(_logger, "MPD Volume Up: %3d", _mpd_volume);
    }
}

void ProjectMWrapper::MPDVolumeDown()
{ 
    if(_mpd_volume>0){
        --_mpd_volume;
        mpd_run_set_volume(mpdc, _mpd_volume);
        Poco::NotificationCenter::defaultCenter().postNotification(
            new DisplayToastNotification(Poco::format("MPD Volume Down: %3d", _mpd_volume)));
            poco_information_f1(_logger, "MPD Volume Down: %3d", _mpd_volume);
    }
}

void ProjectMWrapper::MPDGetStatus()
{
    if (!mpd_command_list_begin(mpdc, true) ||
        !mpd_send_status(mpdc) ||
        !mpd_send_current_song(mpdc) ||
        !mpd_command_list_end(mpdc)
    ) printErrorAndExit();

    struct mpd_status *status = mpd_recv_status(mpdc);
    if (status == NULL) printErrorAndExit();
    if(mpd_status_get_state(status) == MPD_STATE_PLAY) _mpdPlaying = true;
    if(mpd_status_get_state(status) == MPD_STATE_PAUSE) _mpdPlaying = false;

    if (mpd_status_get_state(status) == MPD_STATE_PLAY ||
        mpd_status_get_state(status) == MPD_STATE_PAUSE) {
            if (!mpd_response_next(mpdc)) printErrorAndExit();

            struct mpd_song *song = mpd_recv_song(mpdc);
            if (song != NULL) {
                _songURI = mpd_song_get_uri(song);
                _songName = _songURI.substr(_songURI.find_last_of("/")+1);
                mpd_song_free(song);
                if(_songName != _songNameLast){
                    Poco::NotificationCenter::defaultCenter().postNotification(
                        new DisplayToastNotification(Poco::format("MPD: %s", std::string(_songName))));
                    poco_information_f1(_logger, "Playing: %s", std::string(_songName));
                    _songNameLast = _songName;
                }
                if(_songURI != _songURILast){
                    _songURILast = _songURI;
                    _songPos = mpd_status_get_song_pos(status);
                }
            }

            sprintf(infobuffer," #%i/%u  %3i:%02i / %i:%02i [R%i S%i] VOL: %3i\n",
                    mpd_status_get_song_pos(status) + 1,
                    mpd_status_get_queue_length(status),
                    mpd_status_get_elapsed_time(status) / 60,
                    mpd_status_get_elapsed_time(status) % 60,
                    mpd_status_get_total_time(status) / 60,
                    mpd_status_get_total_time(status) % 60,
                    mpd_status_get_repeat(status),
                    mpd_status_get_single(status),
                    mpd_status_get_volume(status)
                    );
            //printf("%s\n",infobuffer);
            _songInfo.assign(infobuffer);
            _mpd_repeat = mpd_status_get_repeat(status);
            _mpd_single = mpd_status_get_single(status);
            _mpd_volume = mpd_status_get_volume(status);
    }
    if (!mpd_response_finish(mpdc)) printErrorAndExit();
}


std::string ProjectMWrapper::MPDGetSongName(){
    return _songName;
}

std::string ProjectMWrapper::MPDGetSongInfo(){
    return _songInfo;
}

uint ProjectMWrapper::MPDGetSongId(uint pos){
    struct mpd_song* song = mpd_run_get_queue_song_id(mpdc,pos);
    if (song == NULL) printErrorAndExit();
    return mpd_song_get_id(song);
}

void ProjectMWrapper::MPDNext(){
    mpd_run_next(mpdc);
}

void ProjectMWrapper::MPDPrev(){
    mpd_run_previous(mpdc);
}

void ProjectMWrapper::MPDStop(){
    mpd_run_stop(mpdc);
}

void ProjectMWrapper::MPDPlay(){
    mpd_run_play(mpdc);
    MPDGetStatus();
}
void ProjectMWrapper::MPDPlayId(uint i){
    mpd_response_finish(mpdc);
    MPDGetStatus();
    mpd_run_play_id(mpdc,i);
    MPDGetStatus();
}

void ProjectMWrapper::MPDPlayPos(uint i){
    mpd_response_finish(mpdc);
    mpd_run_play_pos(mpdc,i);
    MPDGetStatus();
    
}

void ProjectMWrapper::MPDPause(){
    if(_mpdPlaying){
        mpd_run_pause(mpdc,true);
    }else{
        mpd_run_pause(mpdc,false);
    }
}


struct mpd_connection* ProjectMWrapper::MPDGetConnection(){
    return mpdc;
}

bool ProjectMWrapper::MPDConnect()
{
    mpdc = mpd_connection_new(_MPDConfigView->getString("mpd.host").c_str(), _MPDConfigView->getInt("mpd.port",6600), 50000);
    if (mpd_connection_get_error(mpdc) != MPD_ERROR_SUCCESS) {
        fprintf(stderr, "%s\n", mpd_connection_get_error_message(mpdc));
        mpd_connection_free(mpdc);
        return false;
    }else{
        printf("mpd connected ;)\n");
        return true;
    }   
}

void ProjectMWrapper::MPDListFilesPreview(const char* name)
{
    printf("MPDListFilesPreview\n");
    if (!mpd_command_list_begin(mpdc, true) ||
        !mpd_send_status(mpdc) ||
        !mpd_send_list_playlist_meta(mpdc, name) ||
        !mpd_command_list_end(mpdc)
    ) printErrorAndExit();
    struct mpd_status *status = mpd_recv_status(mpdc);
    if (status == NULL) printErrorAndExit();
    queue_size = mpd_status_get_queue_length(status);
    if (!mpd_response_next(mpdc)) printErrorAndExit();
    struct mpd_song *song;
    mpd_preview.clear();
    while ((song = mpd_recv_song(mpdc)) != NULL) {
        std::string songname = mpd_song_get_uri(song);
        mpd_preview.push_back(songname);
        mpd_song_free(song);
    }
    if (mpd_connection_get_error(mpdc) != MPD_ERROR_SUCCESS ||
        !mpd_response_finish(mpdc)
    ) printErrorAndExit();
}


void ProjectMWrapper::MPDListFiles()
{
    if (!mpd_command_list_begin(mpdc, true) ||
        !mpd_send_status(mpdc) ||
        !mpd_send_list_queue_meta(mpdc) ||
        !mpd_command_list_end(mpdc)
    ) printErrorAndExit();
    struct mpd_status *status = mpd_recv_status(mpdc);
    if (status == NULL) printErrorAndExit();
    queue_size = mpd_status_get_queue_length(status);
    if (!mpd_response_next(mpdc)) printErrorAndExit();
    struct mpd_song *song;
    mpd_queue.clear();
    while ((song = mpd_recv_song(mpdc)) != NULL) {
        std::string songname = mpd_song_get_uri(song);
        mpd_queue.push_back(songname);
        mpd_song_free(song);
    }
    if (mpd_connection_get_error(mpdc) != MPD_ERROR_SUCCESS ||
        !mpd_response_finish(mpdc)
    ) printErrorAndExit();
}

void ProjectMWrapper::MPDListPlaylists()
{
    if (!mpd_command_list_begin(mpdc, true) ||
        !mpd_send_list_playlists(mpdc) ||
        !mpd_command_list_end(mpdc)
    ) printErrorAndExit();
    mpd_playlists.clear();
    struct mpd_playlist *playlist;
    while ((playlist = mpd_recv_playlist(mpdc)) != NULL) {
            std::string playlistname = mpd_playlist_get_path(playlist);
            mpd_playlists.push_back(playlistname);
            //printf("playlists: added '%s'\n",playlistname.c_str());
            mpd_playlist_free(playlist);
    }
    if (mpd_connection_get_error(mpdc) != MPD_ERROR_SUCCESS ||
        !mpd_response_finish(mpdc)
    ) printErrorAndExit();
    std::sort(mpd_playlists.begin(), mpd_playlists.end()); //,          [] (MPDPlaylist const& a, MPDPlaylist const& b) { return a.v < b.v; });
    
}


std::string ProjectMWrapper::MPDQGet(uint i){    return mpd_queue.at(i);}
bool ProjectMWrapper::MPDQDel(uint i){    fprintf(stderr,"mpdqdel: %d\n",i);mpd_queue.erase(mpd_queue.begin()+i); return true;}
size_t      ProjectMWrapper::MPDQSize(){    return mpd_queue.size();}
std::string ProjectMWrapper::MPDPLGet(uint i){    return mpd_playlists.at(i);}
size_t      ProjectMWrapper::MPDPLSize(){    return mpd_playlists.size();}
std::string ProjectMWrapper::MPDPVGet(uint i){    return mpd_preview.at(i);}
size_t      ProjectMWrapper::MPDPVSize(){    return mpd_preview.size();}

void ProjectMWrapper::MPDQueueAddPlaylist(const char* name, bool clear_queue){
    //printf("Playlist '%s' [%d]\n",name,clear_queue?1:0);
    if(clear_queue){
        if (!mpd_command_list_begin(mpdc, true) ||
            !mpd_send_clear(mpdc) ||
            !mpd_send_load(mpdc, name) ||
            !mpd_command_list_end(mpdc)
        ) printErrorAndExit();
    }else{
        if (!mpd_command_list_begin(mpdc, true) ||
            !mpd_send_load(mpdc, name) ||
            !mpd_command_list_end(mpdc)
        ) printErrorAndExit();
        _mpd_queue_clear_add = true;
        //printf("->Playlist '%s' [%d]\n",name,clear_queue?1:0);
        Poco::NotificationCenter::defaultCenter().postNotification(
            new DisplayToastNotification(Poco::format("Playlist '%s' added", std::string(name))));
    }
    
    //printf("Playlist '%s' loaded\n",name);
    mpd_response_finish(mpdc);
    MPDListFiles();
    MPDGetStatus();
    mpd_run_play(mpdc);
    MPDGetStatus();
    
}
void ProjectMWrapper::MPDQueueAdd(const char* name){
    if (!mpd_command_list_begin(mpdc, true) ||
            !mpd_send_add(mpdc, name) ||
            !mpd_command_list_end(mpdc)
        ) printErrorAndExit();
    mpd_response_finish(mpdc);
    Poco::NotificationCenter::defaultCenter().postNotification(
        new DisplayToastNotification(Poco::format("Item '%s' added", std::string(name))));
}

void ProjectMWrapper::MPDQueueDelete(uint id){
    if (!mpd_command_list_begin(mpdc, true) ||
            !mpd_send_delete(mpdc, id) ||
            !mpd_command_list_end(mpdc)
        ) printErrorAndExit();
    mpd_response_finish(mpdc);
}
