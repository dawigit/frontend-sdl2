#include "ProjectMGUI.h"

#include "AnonymousProFont.h"
#include "LiberationSansFont.h"
#include "DejavuFont.h"
#include "ProjectMWrapper.h"
#include "SDLRenderingWindow.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"


#include <Poco/NotificationCenter.h>

#include <Poco/Util/Application.h>

#include <utility>

const char* ProjectMGUI::name() const
{
    return "Preset Selection GUI";
}

void ProjectMGUI::initialize(Poco::Util::Application& app)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    Poco::Path userConfigurationDir = Poco::Path::configHome();
    userConfigurationDir.makeDirectory().append("projectM/");
    userConfigurationDir.setFileName(app.config().getString("application.baseName") + ".UI.ini");
    _uiIniFileName = userConfigurationDir.toString();

    io.IniFilename = _uiIniFileName.c_str();

    ImGui::StyleColorsDark();

    auto& renderingWindow = Poco::Util::Application::instance().getSubsystem<SDLRenderingWindow>();
    auto& projectMWrapper = Poco::Util::Application::instance().getSubsystem<ProjectMWrapper>();

    _projectMWrapper = &projectMWrapper;
    _renderingWindow = renderingWindow.GetRenderingWindow();
    _glContext = renderingWindow.GetGlContext();

    ImGui_ImplSDL2_InitForOpenGL(_renderingWindow, _glContext);
    ImGui_ImplOpenGL3_Init("#version 130");

    UpdateFontSize();

    // Set a sensible minimum window size to prevent layout assertions
    auto& style = ImGui::GetStyle();
    style.WindowMinSize = {128, 128};

    std::string DataDir = Poco::Path::dataHome().append("projectMSDL/");
    std::string file = DataDir;
    //7printf("DataDir: '%s'\n",DataDir.c_str());
    //7printf("locked: '%s'\n",std::string(file+"locked.png").c_str());
    bool ret = LoadTextureFromFile(std::string(file+"locked.png").c_str(), &lock_image_texture, &lock_image_width, &lock_image_height);
    IM_ASSERT(ret);
    ret = LoadTextureFromFile(std::string(file+"shuffle.png").c_str(), &shuffle_image_texture, &shuffle_image_width, &shuffle_image_height);
    IM_ASSERT(ret);
    ret = LoadTextureFromFile(std::string(file+"star0.png").c_str(), &star0_image_texture, &star0_image_width, &star0_image_height);
    IM_ASSERT(ret);
    ret = LoadTextureFromFile(std::string(file+"star1.png").c_str(), &star1_image_texture, &star1_image_width, &star1_image_height);
    IM_ASSERT(ret);
    
    Poco::NotificationCenter::defaultCenter().addObserver(_displayToastNotificationObserver);
}

void ProjectMGUI::uninitialize()
{
    Poco::NotificationCenter::defaultCenter().removeObserver(_displayToastNotificationObserver);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    _projectMWrapper = nullptr;
    _renderingWindow = nullptr;
    _glContext = nullptr;
}

void ProjectMGUI::UpdateFontSize()
{
    ImGuiIO& io = ImGui::GetIO();

    auto displayIndex = SDL_GetWindowDisplayIndex(_renderingWindow);
    if (displayIndex < 0)
    {
        poco_debug_f1(_logger, "Could not get display index for application window: %s", std::string(SDL_GetError()));
        return;
    }

    auto newScalingFactor = GetScalingFactor();

    // Only interested in changes of .05 or more
    if (std::abs(_textScalingFactor - newScalingFactor) < 0.05)
    {
        return;
    }

    poco_debug_f3(_logger, "Scaling factor change for display %?d: %hf -> %hf", displayIndex, _textScalingFactor, newScalingFactor);

    _textScalingFactor = newScalingFactor;

    ImFontConfig config;
    config.MergeMode = true;

    io.Fonts->Clear();
    _uiFont = io.Fonts->AddFontFromMemoryCompressedTTF(&AnonymousPro_compressed_data, AnonymousPro_compressed_size, floor(48.0f * _textScalingFactor));
    _dejavuFont = io.Fonts->AddFontFromMemoryCompressedTTF(&Dejavu_compressed_data, Dejavu_compressed_size, floor(64.0f * _textScalingFactor));
    _toastFont = io.Fonts->AddFontFromMemoryCompressedTTF(&LiberationSans_compressed_data, LiberationSans_compressed_size, floor(96.0f * _textScalingFactor));
    _dejavuFontL = io.Fonts->AddFontFromMemoryCompressedTTF(&Dejavu_compressed_data, Dejavu_compressed_size, floor(128.0f * _textScalingFactor));
    _kaffeeFont = io.Fonts->AddFontFromMemoryCompressedTTF(&LiberationSans_compressed_data, LiberationSans_compressed_size, floor(148.0f * _textScalingFactor));
    //_freeFont = io.Fonts->AddFontFromMemoryCompressedTTF(&FreeMonoBold_compressed_data, FreeMonoBold_compressed_size, floor(96.0f * _textScalingFactor));
    io.Fonts->Build();
    ImGui_ImplOpenGL3_CreateFontsTexture();

    ImGui::GetStyle().ScaleAllSizes(1.0);
}

void ProjectMGUI::ProcessInput(const SDL_Event& event)
{
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void ProjectMGUI::Toggle()
{
    _visible = !_visible;
}

void ProjectMGUI::Visible(bool visible)
{
    _visible = visible;
}

bool ProjectMGUI::Visible() const
{
    return _visible;
}

void ProjectMGUI::Draw()
{
    // Don't render UI at all if there's no need.
    if (!_toast && !_visible && !_permTextVisible)
    {
        return;
    }

    ImGui_ImplSDL2_NewFrame();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    float secondsSinceLastFrame = .0f;
    if (_lastFrameTicks == 0)
    {
        _lastFrameTicks = SDL_GetTicks64();
    }
    else
    {
        auto currentFrameTicks = SDL_GetTicks64();
        secondsSinceLastFrame = static_cast<float>(currentFrameTicks - _lastFrameTicks) * .001f;
        _lastFrameTicks = currentFrameTicks;
    }

    if (_toast)
    {
        if(_toast->getToastText().find("PRESET:")==0){
            _permText = _toast->getToastText().substr(strlen("PRESET:")-1);
            _permText = _permText.substr(_permText.find_last_of("/")+1); // remove path prefix
            _permText = _permText.substr(0,_permText.size()-5); // remove '.milk' extension
            //fprintf(stderr,"_permText: '%s'\n",_permText.c_str());
        }
        //if (!_toast->Draw(secondsSinceLastFrame)){_toast.reset();}
        _toast.reset();
    }

    if (_visible)
    {
        _mainMenu.Draw();
        _settingsWindow.Draw();
        _aboutWindow.Draw();
        _helpWindow.Draw();
    }
    
    //either menu or permanent info visible
    if(_permTextVisible && !_visible)
    { 
        if (_permText.size()>0){       
            DrawPermText(_permText);    
        }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool ProjectMGUI::WantsKeyboardInput()
{
    auto& io = ImGui::GetIO();
    return io.WantCaptureKeyboard;
}

bool ProjectMGUI::WantsMouseInput()
{
    auto& io = ImGui::GetIO();
    return io.WantCaptureMouse;
}

void ProjectMGUI::PushToastFont()
{
    ImGui::PushFont(_toastFont);
}

void ProjectMGUI::PushUIFont()
{
    ImGui::PushFont(_uiFont);
}

void ProjectMGUI::PushFreeFont()
{
    ImGui::PushFont(_freeFont);
}

void ProjectMGUI::PopFont()
{
    ImGui::PopFont();
}

void ProjectMGUI::ShowSettingsWindow()
{
    _settingsWindow.Show();
}

void ProjectMGUI::ShowAboutWindow()
{
    _aboutWindow.Show();
}

void ProjectMGUI::ShowHelpWindow()
{
    _helpWindow.Show();
}

float ProjectMGUI::GetScalingFactor()
{
    int renderWidth;
    int renderHeight;

    SDL_GetWindowSize(_renderingWindow, &windowWidth, &windowHeight);
    SDL_GL_GetDrawableSize(_renderingWindow, &renderWidth, &renderHeight);

    // If the OS has a scaled UI, this will return the inverse factor. E.g. if the display is scaled to 200%,
    // the renderWidth (in actual pixels) will be twice as much as the "virtual" unscaled window width.
    return ((static_cast<float>(windowWidth) / static_cast<float>(renderWidth)) + (static_cast<float>(windowHeight) / static_cast<float>(renderHeight))) * 0.5f;
}

void ProjectMGUI::DisplayToastNotificationHandler(const Poco::AutoPtr<DisplayToastNotification>& notification)
{
    if (Poco::Util::Application::instance().config().getBool("projectM.displayToasts", true))
    {
        _toast = std::make_unique<ToastMessage>(notification->ToastText(), 3.0f);
    }
}

void ProjectMGUI::DrawPermText(std::string permText)
{
    constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration |
                                             ImGuiWindowFlags_AlwaysAutoResize |
                                             ImGuiWindowFlags_NoSavedSettings |
                                             ImGuiWindowFlags_NoFocusOnAppearing |
                                             ImGuiWindowFlags_NoNav |
                                             ImGuiWindowFlags_NoMove;
    
    bool locked = false;
    bool shuffle = false;
    if (Poco::Util::Application::instance().config().getBool("projectM.presetLocked", true)){
        locked = true;
    }
    if (Poco::Util::Application::instance().config().getBool("projectM.shuffleEnabled", true)){
        shuffle = true;
    }
    if(!_visible){

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always, ImVec2(0.0f, 0.0f));
    float alpha = 0.6f;
    int textWidth;
    int playcountWidth;
    ImGui::SetNextWindowBgAlpha(0.35f * alpha);
    ImGui::Begin("PermText", &_permTextVisible, windowFlags);
    ImFont* FontList[5]{_kaffeeFont,_dejavuFontL,_toastFont,_dejavuFont,_uiFont};
    int font_index = 0;
    int cursor_x = 0;
    if(shuffle){
        ImGui::Image((ImTextureID)(intptr_t)shuffle_image_texture, ImVec2(shuffle_image_width, shuffle_image_height));    
        ImGui::SameLine();
        cursor_x += shuffle_image_width;
    }
    if(locked){
        ImGui::Image((ImTextureID)(intptr_t)lock_image_texture, ImVec2(lock_image_width, lock_image_height));
        ImGui::SameLine();
        cursor_x += lock_image_width;
    }
    cursor_x += 32;
    SDL_GetWindowSize(_renderingWindow, &windowWidth, &windowHeight);
    float ww = windowWidth - cursor_x;
    ImGui::PushFont(FontList[font_index]);
    
    do{
        ImGui::PopFont();
        ImGui::PushFont(FontList[font_index++]);
        if(font_index>4)break;
        textWidth   = ImGui::CalcTextSize(_permText.c_str()).x;
    
    
    }while(textWidth > ww);
    ww += cursor_x;
    //printf("cx %d tw %d ww %d \n", cursor_x, textWidth, windowWidth);
    ImGui::Text(" %s ", _permText.c_str());
    PopFont();

    int rating = _projectMWrapper->GetRating();
    int playcount = _projectMWrapper->GetPlayCount();
    std::string spc = std::to_string(playcount);

    int i=0;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    while(i<rating){
        ImGui::Image((ImTextureID)(intptr_t)star1_image_texture, ImVec2(star1_image_width, star1_image_height));
        ImGui::SameLine();
        ++i;
    }
    int i2=5-rating;
    while(i2){
        ImGui::Image((ImTextureID)(intptr_t)star0_image_texture, ImVec2(star0_image_width, star0_image_height));
        ImGui::SameLine();  
        --i2;
    }
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(2*star0_image_width, 0.0f));
  
    ImGui::PushFont(_kaffeeFont);
    playcountWidth = ImGui::CalcTextSize(spc.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth()-(playcountWidth+24));

    ImGui::Text("%s",spc.c_str());
    ImGui::PopFont();
    
    if (!_broughtToFront){
        ImGui::SetWindowFocus();
        _broughtToFront = true;
    }
    ImGui::End();

    } // if(!Visible()){
    
}



// Simple helper function to load an image into a OpenGL texture with common settings
bool ProjectMGUI::LoadTextureFromMemory(const void* data, size_t data_size, GLuint* out_texture, int* out_width, int* out_height)
{
    // Load from file
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load_from_memory((const unsigned char*)data, (int)data_size, &image_width, &image_height, NULL, 4);
    if (image_data == NULL)
        return false;

    // Create a OpenGL texture identifier
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Upload pixels into texture
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);

    *out_texture = image_texture;
    *out_width = image_width;
    *out_height = image_height;

    return true;
}

// Open and read a file, then forward to LoadTextureFromMemory()
bool ProjectMGUI::LoadTextureFromFile(const char* file_name, GLuint* out_texture, int* out_width, int* out_height)
{
    FILE* f = fopen(file_name, "rb");
    if (f == NULL)
        return false;
    fseek(f, 0, SEEK_END);
    size_t file_size = (size_t)ftell(f);
    if (file_size == -1)
        return false;
    fseek(f, 0, SEEK_SET);
    void* file_data = IM_ALLOC(file_size);
    fread(file_data, 1, file_size, f);
    bool ret = LoadTextureFromMemory(file_data, file_size, out_texture, out_width, out_height);
    IM_FREE(file_data);
    return ret;
}