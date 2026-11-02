#pragma once
// AP_MULTIWORLD_HUD_V082: styled, scaleable notifications and synthesized progress chime.
// Included from gpu/video.cpp AFTER the roulette UI declarations.
// ImGui and SDL are already initialized by video.cpp.
namespace APMultiplayerOverlay
{
    struct Card
    {
        APClient::MultiplayerEvent event;
        double created = 0.0;
    };

    static bool initialized = false;
    static bool enabled = true;
    static bool progressiveSound = true;
    static bool preview = false;
    static int hudScale = 100;
    static int sfxVolume = 42;
    static int position = 0; // 0 lower right, 1 upper left
    static std::vector<Card> cards;
    static SDL_AudioDeviceID audioDevice = 0;
    static bool audioUnavailable = false;

    static std::filesystem::path SettingsPath()
    {
        return APRingRoulette::SettingsPath().parent_path() / "ap_multiplayer_hud_v082.json";
    }
    static void Load()
    {
        if (initialized) return;
        initialized = true;
        try
        {
            std::ifstream file(SettingsPath());
            if (!file) return;
            const auto j = nlohmann::json::parse(file, nullptr, false);
            if (!j.is_object()) return;
            if (j.contains("enabled") && j["enabled"].is_boolean()) enabled = j["enabled"].get<bool>();
            if (j.contains("progressive_sound") && j["progressive_sound"].is_boolean()) progressiveSound = j["progressive_sound"].get<bool>();
            if (j.contains("scale") && j["scale"].is_number_integer()) hudScale = std::clamp(j["scale"].get<int>(), 65, 165);
            if (j.contains("volume") && j["volume"].is_number_integer()) sfxVolume = std::clamp(j["volume"].get<int>(), 0, 100);
            if (j.contains("position") && j["position"].is_number_integer()) position = std::clamp(j["position"].get<int>(), 0, 1);
        }
        catch (...) { /* Optional cosmetic settings must never affect gameplay. */ }
    }
    static void Save()
    {
        try
        {
            const auto path = SettingsPath();
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path, std::ios::trunc);
            if (file)
                file << nlohmann::json({
                    {"enabled",enabled}, {"progressive_sound",progressiveSound},
                    {"scale",hudScale}, {"volume",sfxVolume}, {"position",position}
                }).dump(2) << '\n';
        }
        catch (...) { }
    }
    static void Chime()
    {
        if (!progressiveSound || sfxVolume <= 0) return;
        if (!audioDevice && !audioUnavailable)
        {
            SDL_AudioSpec wanted{};
            wanted.freq = 44100;
            wanted.format = AUDIO_F32SYS;
            wanted.channels = 2;
            wanted.samples = 1024;
            wanted.callback = nullptr;
            audioDevice = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
            if (!audioDevice) audioUnavailable = true;
            else SDL_PauseAudioDevice(audioDevice, 0);
        }
        if (!audioDevice) return;
        // Two soft bell notes with a fast attack and exponential release.
        // 44.1kHz floating-point stereo, generated in memory: no external audio asset.
        constexpr int rate = 44100;
        constexpr int frames = 14000;
        constexpr float pi = 3.14159265358979323846f;
        std::vector<float> pcm(frames * 2);
        const float master = 0.16f * (static_cast<float>(sfxVolume) / 100.0f);
        for (int n = 0; n < frames; ++n)
        {
            const float t = static_cast<float>(n) / rate;
            const float env1 = (1.0f - std::exp(-t * 140.0f)) * std::exp(-t * 14.5f);
            const float dt = t - 0.086f;
            const float env2 = dt > 0.f ? (1.0f - std::exp(-dt * 145.f)) * std::exp(-dt * 14.5f) : 0.f;
            const float a = (std::sin(2.f*pi*784.f*t) + .24f*std::sin(2.f*pi*1568.f*t)) * env1;
            const float b = dt > 0.f ? (std::sin(2.f*pi*1174.66f*dt) + .22f*std::sin(2.f*pi*2349.32f*dt)) * env2 : 0.f;
            const float sample = master * (a + b);
            pcm[n*2] = sample;
            pcm[n*2 + 1] = sample;
        }
        // Avoid sound storms in a busy multiworld.
        if (SDL_GetQueuedAudioSize(audioDevice) > static_cast<Uint32>(rate * 2 * sizeof(float) / 4))
            SDL_ClearQueuedAudio(audioDevice);
        SDL_QueueAudio(audioDevice, pcm.data(), static_cast<Uint32>(pcm.size() * sizeof(float)));
    }

    static std::string Compact(const std::string& value, std::size_t maxCharacters)
    {
        // Keep UTF-8 character boundaries on truncation.
        if (value.size() <= maxCharacters) return value;
        std::size_t n = maxCharacters > 3 ? maxCharacters - 3 : 0;
        while (n > 0 && (static_cast<unsigned char>(value[n]) & 0xC0u) == 0x80u) --n;
        return value.substr(0,n) + "...";
    }
    static ImU32 WithAlpha(ImU32 rgba, float alpha)
    {
        return (rgba & 0x00FFFFFFu) | (static_cast<ImU32>(static_cast<float>((rgba >> 24) & 255u) * alpha) << 24);
    }

    static void DrawOverlay()
    {
        Load();
        const auto fresh = APClient::DrainMultiplayerEvents();
        const double now = ImGui::GetTime();
        bool playChime = false;
        for (const auto& event : fresh)
        {
            playChime |= event.isProgression && event.receivedLocally;
            cards.push_back({event, now});
            if (cards.size() > 24) cards.erase(cards.begin());
        }
        if (playChime) Chime();
        // Process every network event even if overlay disabled; no queued pop-ups on re-enable.
        if (!enabled) { cards.clear(); return; }
        cards.erase(std::remove_if(cards.begin(), cards.end(),
            [now](const Card& c) { return now - c.created > 6.8; }), cards.end());
        if (cards.empty()) return;

        const auto* viewport = ImGui::GetMainViewport();
        const float userScale = static_cast<float>(hudScale) / 100.f;
        const float screenScale = std::clamp(viewport->Size.y / 1080.f, .72f, 1.45f);
        const float scale = screenScale * userScale;
        const float w = std::min(420.f * scale, viewport->Size.x - 36.f);
        if (w <= 220.f * scale) return;
        const float h = 94.f * scale;
        const float gap = 11.f * scale;
        const float margin = 22.f * screenScale;
        const std::size_t visible = std::min<std::size_t>(cards.size(), 3);
        const float top = position == 0
            ? viewport->Pos.y + viewport->Size.y - margin - (h + gap) * visible + gap
            : viewport->Pos.y + 67.f * screenScale;
        const float x = position == 0 ? viewport->Pos.x + viewport->Size.x - w - margin : viewport->Pos.x + margin;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = ImFontAtlasSnapshot::GetFont("FOT-SeuratPro-M.otf");
        if (!font) font = ImGui::GetFont();

        for (std::size_t i = 0; i < visible; ++i)
        {
            const auto& c = cards[cards.size() - visible + i];
            const auto& e = c.event;
            const double age = now - c.created;
            const float fadeIn = std::clamp(static_cast<float>(age / .28), 0.f, 1.f);
            const float fadeOut = std::clamp(static_cast<float>((6.8 - age) / .72), 0.f, 1.f);
            const float fade = fadeIn * fadeOut;
            const float offset = (1.f - fadeIn) * 28.f * scale;
            const float y = top + static_cast<float>(i) * (h + gap);
            const ImVec2 a(x + offset, y);
            const ImVec2 b(a.x + w, a.y + h);
            const ImU32 trim = e.isProgression ? IM_COL32(250,202,91,255) : IM_COL32(79,211,250,255);
            dl->AddRectFilled(ImVec2(a.x + 4*scale,a.y + 5*scale), ImVec2(b.x + 4*scale,b.y + 5*scale), WithAlpha(IM_COL32(0,4,15,150),fade), 17*scale);
            dl->AddRectFilled(a,b,WithAlpha(IM_COL32(8,31,66,247),fade),17*scale);
            // Subtle rounded highlight rather than a squared panel inside the chrome frame.
            dl->AddRectFilled(ImVec2(a.x+3*scale,a.y+3*scale), ImVec2(b.x-3*scale,a.y+27*scale), WithAlpha(IM_COL32(48,106,154,72),fade),14*scale);
            dl->AddRect(a,b,WithAlpha(IM_COL32(146,214,245,225),fade),17*scale,0,1.7f*scale);
            dl->AddLine(ImVec2(a.x+17*scale,a.y+2*scale),ImVec2(b.x-17*scale,a.y+2*scale),WithAlpha(IM_COL32(195,235,255,190),fade),1.5f*scale);
            dl->AddRectFilled(ImVec2(a.x + 2*scale,a.y + 16*scale),ImVec2(a.x + 6*scale,b.y - 16*scale),WithAlpha(trim,fade),3*scale);
            // Round badge, consistent with the F8 metallic neon language.
            const ImVec2 center(a.x + 34*scale, a.y + 45*scale);
            dl->AddCircleFilled(center,21*scale,WithAlpha(IM_COL32(7,27,59,240),fade),32);
            dl->AddCircle(center,21*scale,WithAlpha(trim,fade),32,1.7f*scale);
            const char* glyph = e.receivedLocally ? "+" : ">";
            const float glyphSize = 29*scale;
            const ImVec2 glyphW = font->CalcTextSizeA(glyphSize,FLT_MAX,0.f,glyph);
            dl->AddText(font,glyphSize,ImVec2(center.x - glyphW.x*.5f,center.y-glyphW.y*.5f),WithAlpha(trim,fade),glyph);

            const std::string label = e.isProgression ? "PROGRESSION ITEM" : "MULTIWORLD ITEM";
            const std::string itemName = Compact(APClient::ResolveMultiplayerItemName(e), 47);
            const std::string route = Compact(e.sourceName + "  >  " + e.destinationName, 53);
            const float tx = a.x + 66*scale;
            auto text = [&](const std::string& content, float yy, float size, ImU32 color) {
                const ImVec2 at(tx, a.y+yy*scale);
                float fontSize = size*scale;
                const float maxWidth = std::max(40.f, b.x-tx-13*scale);
                const float measured = font->CalcTextSizeA(fontSize,FLT_MAX,0.f,content.c_str()).x;
                if (measured > maxWidth) fontSize *= maxWidth / measured;
                // Round game font + dark drop-shadow for readable bubble-like lettering.
                dl->AddText(font,fontSize,ImVec2(at.x+1*scale,at.y+1.5f*scale),WithAlpha(IM_COL32(0,8,22,220),fade),content.c_str());
                dl->AddText(font,fontSize,at,WithAlpha(color,fade),content.c_str());
            };
            text(label, 10, 13, trim);
            text(itemName, 29, 23, IM_COL32(250,252,255,255));
            text(route, 63, 15, IM_COL32(174,208,236,255));
            // Timeline shows the auto-dismiss progression.
            dl->AddRectFilled(ImVec2(a.x+14*scale,b.y-6*scale),
                ImVec2(a.x+(14+(w/scale-28)*std::clamp(static_cast<float>((6.8-age)/6.8),0.f,1.f))*scale,b.y-4*scale),WithAlpha(trim,fade),1.1f*scale);
        }
    }

    static void DrawSettings()
    {
        Load();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(.43f,.84f,1.f,1.f),"MULTIWORLD  |  LIVE ITEM FEED");
        ImGui::TextWrapped("Every player-to-player item transfer appears in the game HUD.");
        if (ImGui::Checkbox("Show item notifications##APFeedVisible", &enabled)) Save();
        if (ImGui::SliderInt("HUD size##APFeedScale", &hudScale, 65, 165, "%d%%")) {}
        if (ImGui::IsItemDeactivatedAfterEdit()) Save();
        const char* places[] = {"Bottom right", "Top left"};
        if (ImGui::Combo("Position##APFeedPosition", &position, places, IM_ARRAYSIZE(places))) Save();
        if (ImGui::Checkbox("Chime for progression items received##APFeedSFX", &progressiveSound)) Save();
        if (ImGui::SliderInt("Chime volume##APFeedVolume", &sfxVolume, 0, 100, "%d%%")) {}
        if (ImGui::IsItemDeactivatedAfterEdit()) Save();
        if (ImGui::Button("PREVIEW NOTIFICATION##APFeedPreview", ImVec2(-1,36)))
        {
            APClient::MultiplayerEvent sample{};
            sample.sourceName = "Player One";
            sample.destinationName = "You";
            sample.itemName = "Stomping Shoes";
            sample.isProgression = true;
            sample.receivedLocally = false; // Preview must not play an item receipt sound.
            cards.push_back({sample,ImGui::GetTime()});
            if (cards.size() > 24) cards.erase(cards.begin());
        }
        if (ImGui::Button("PREVIEW CHIME##APFeedChime", ImVec2(-1,30))) Chime();
        ImGui::TextDisabled("Animated neon glass style / game font / 3 simultaneous cards");
        ImGui::TextDisabled("Settings save automatically, no gameplay or save files altered.");
    }
}
